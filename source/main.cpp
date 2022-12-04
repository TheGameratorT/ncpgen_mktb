#include <iostream>
#include <vector>
#include <algorithm>
#include <fstream>
#include <memory>
#include <filesystem>

#include <pugixml.hpp>

#include "types.hpp"
#include "ndsheader.hpp"

namespace fs = std::filesystem;

#define OVERLAY_FLAG_COMP 1
#define OVERLAY_FLAG_AUTH 2

struct OvtEntry
{
	u32 overlayID;
	u32 ramAddress;
	u32 ramSize;
	u32 bssSize;
	u32 sinitStart;
	u32 sinitEnd;
	u32 fileID;
	u32 compressed : 24; // size of compressed "ramSize"
	u32 flag : 8;
};

static std::size_t nodeCount(pugi::xml_node node, const char* childName)
{
	return std::distance(node.children(childName).begin(), node.children(childName).end());
}

static bool generateOvt(pugi::xml_node romInfoNode, bool isArm9, const fs::path& ovtFilePath)
{
	pugi::xml_node armOvtNode = romInfoNode.child(isArm9 ? "ARM9Ovt" : "ARM7Ovt");
	std::vector<OvtEntry> ovtEntries(nodeCount(armOvtNode, "RomOVT"));
	auto ovtEntry = ovtEntries.begin();
	for (pugi::xml_node entryNode : armOvtNode)
	{
		if (strcmp(entryNode.name(), "RomOVT") != 0)
			continue;

		std::string flag = entryNode.attribute("Flag").as_string();

		ovtEntry->overlayID = entryNode.attribute("Id").as_int();
		ovtEntry->flag =
			((flag.find("Compressed") != std::string::npos) ? OVERLAY_FLAG_COMP : 0) |
			((flag.find("AuthenticationCode") != std::string::npos) ? OVERLAY_FLAG_AUTH : 0);

		ovtEntry->ramAddress = std::stoi(entryNode.child_value("RamAddress"));
		ovtEntry->ramSize = std::stoi(entryNode.child_value("RamSize"));
		ovtEntry->bssSize = std::stoi(entryNode.child_value("BssSize"));
		ovtEntry->sinitStart = std::stoi(entryNode.child_value("SinitInit"));
		ovtEntry->sinitEnd = std::stoi(entryNode.child_value("SinitInitEnd"));
		ovtEntry->compressed = std::stoi(entryNode.child_value("Compressed"));
		ovtEntry++;
	}

	std::sort(ovtEntries.begin(), ovtEntries.end(), [](const OvtEntry& a, const OvtEntry& b){
		return a.overlayID < b.overlayID;
	});

	std::ofstream ovtFile(ovtFilePath, std::ios::binary);
	if (!ovtFile.is_open())
	{
		std::cout << "Failed to save \"" << ovtFilePath.string() << "\"" << std::endl;
		return false;
	}
	ovtFile.write(reinterpret_cast<const char*>(ovtEntries.data()), ovtEntries.size() * sizeof(OvtEntry));
	ovtFile.close();

	return true;
}

static bool generateOvSyms(const fs::path& ovDir, const fs::path& ov9dir, const fs::path& ov7dir)
{
	fs::create_directories(ov9dir);
	fs::create_directories(ov7dir);

	for (const auto& dirEntry : fs::directory_iterator(ovDir))
	{
		if (!dirEntry.is_regular_file())
			continue;
		std::string fileName = dirEntry.path().filename().string();
		int ovTarget = fileName.starts_with("main_") ? 1 : (fileName.starts_with("sub_") ? 2 : 0);
		if (ovTarget != 0)
		{
			int fileId = std::stoi(fileName.substr(5));
			fs::path srcPath = fs::absolute(dirEntry);
			fs::path dstPath = ((ovTarget == 1 ? ov9dir : ov7dir) / ((ovTarget == 1 ? "overlay9_" : "overlay7_")
				+ std::to_string(fileId) + ".bin"));
			std::error_code errCode;
			fs::create_symlink(srcPath, dstPath, errCode);
			if (errCode)
			{
				std::cout << "Failed to create symbolic link: " << dstPath.make_preferred().string()
					<< "\nAre you perhaps missing elevated privileges?" << std::endl;
				return false;
			}
		}
	}

	return true;
}

static bool restoreOvt(pugi::xml_node romInfoNode, bool isArm9, const fs::path& ovtFilePath)
{
	pugi::xml_node armOvtNode = romInfoNode.child(isArm9 ? "ARM9Ovt" : "ARM7Ovt");
	armOvtNode.remove_children();

	uintmax_t ovtFileSz = fs::file_size(ovtFilePath);
	std::vector<OvtEntry> ovtEntries(ovtFileSz / sizeof(OvtEntry));

	std::ifstream ovtFile(ovtFilePath, std::ios::binary);
	if (!ovtFile.is_open())
	{
		std::cout << "Failed to load \"" << ovtFilePath.string() << "\"" << std::endl;
		return false;
	}
	ovtFile.read(reinterpret_cast<char*>(ovtEntries.data()), ovtEntries.size() * sizeof(OvtEntry));
	ovtFile.close();

	std::string tmp;
	for (const auto& ovtEntry : ovtEntries)
	{
		pugi::xml_node entryNode = armOvtNode.append_child("RomOVT");
		entryNode.append_attribute("Id").set_value((tmp = std::to_string(ovtEntry.overlayID)).c_str());

		const char* flagValue;
		if ((ovtEntry.flag & OVERLAY_FLAG_COMP) && (ovtEntry.flag & OVERLAY_FLAG_AUTH))
		{
			flagValue = "Compressed AuthenticationCode";
		}
		else if (ovtEntry.flag & OVERLAY_FLAG_COMP)
		{
			flagValue = "Compressed";
		}
		else if (ovtEntry.flag & OVERLAY_FLAG_AUTH)
		{
			flagValue = "AuthenticationCode";
		}
		else
		{
			flagValue = "";
		}
		entryNode.append_attribute("Flag").set_value(flagValue);

		auto addField = [&](const char* name, u32 value){
			entryNode.append_child(name).append_child(pugi::node_pcdata).set_value((tmp = std::to_string(value)).c_str());
		};
		addField("RamAddress", ovtEntry.ramAddress);
		addField("RamSize", ovtEntry.ramSize);
		addField("BssSize", ovtEntry.bssSize);
		addField("SinitInit", ovtEntry.sinitStart);
		addField("SinitInitEnd", ovtEntry.sinitEnd);
		addField("Compressed", ovtEntry.compressed);
	}

	return true;
}

int main(int argc, char* argv[])
{
	std::ios::sync_with_stdio(false);

	fs::path xmlPath;

	bool isPrerun = true;
	if (argc == 3)
	{
		isPrerun = argv[1][0] == '0';
		xmlPath = fs::absolute(argv[2]).make_preferred();
	}
	else
	{
		std::cout << "Invalid argument count, must be exactly 2.\n\nSyntax:\nncpgen MODE XML_PATH\n"
		"\nMODE = 0 for pre-built, 1 for post-build"
		"\nXML_PATH = The path of the ROM XML project" << std::endl;
		return 0;
	}

	pugi::xml_document doc;
	std::string xmlPathStr = xmlPath.string();
	pugi::xml_parse_result result = doc.load_file(xmlPathStr.c_str());
	if (!result)
	{
		std::cout << "Failed to load \"" << xmlPathStr << "\"" << std::endl;
		return 1;
	}

	pugi::xml_node romInfoNode = doc.child("NDSProjectFile").child("RomInfo");

	fs::path romfsPath = xmlPath.parent_path();
	fs::path headerFilePath = (romfsPath / "header.bin").make_preferred();
	fs::path arm9ovtPath = (romfsPath / "arm9ovt.bin").make_preferred();
	fs::path arm7ovtPath = (romfsPath / "arm7ovt.bin").make_preferred();
	fs::path armOvDir = (romfsPath / "overlay").make_preferred();
	fs::path arm9ovDir = (romfsPath / "overlay9").make_preferred();
	fs::path arm7ovDir = (romfsPath / "overlay7").make_preferred();

	if (isPrerun)
	{
		// Generate minimalistic header.bin (only contains enough data for ncp)
		pugi::xml_node headerNode = romInfoNode.child("Header");
		auto ndsHeader = std::make_unique<NDSHeader>();
		ndsHeader->arm9.entryAddress = std::stoi(headerNode.child_value("MainEntryAddress"));
		ndsHeader->arm9.ramAddress = std::stoi(headerNode.child_value("MainRamAddress"));
		ndsHeader->arm7.entryAddress = std::stoi(headerNode.child_value("SubEntryAddress"));
		ndsHeader->arm7.ramAddress = std::stoi(headerNode.child_value("SubRamAddress"));
		ndsHeader->arm9AutoLoadListHookOffset = std::stoi(headerNode.child_value("MainAutoloadDone"));
		ndsHeader->arm7AutoLoadListHookOffset = std::stoi(headerNode.child_value("SubAutoloadDone"));

		std::ofstream headerFile(headerFilePath, std::ios::binary);
		if (!headerFile.is_open())
		{
			std::cout << "Failed to save \"" << headerFilePath.string() << "\"" << std::endl;
			return 1;
		}
		headerFile.write(reinterpret_cast<const char*>(ndsHeader.get()), sizeof(NDSHeader));
		headerFile.close();

		// Generate arm9ovt.bin and arm7ovt.bin
		if (!generateOvt(romInfoNode, true, arm9ovtPath)) return 1;
		if (!generateOvt(romInfoNode, false, arm7ovtPath)) return 1;

		// Generate symbolic links
		if (!generateOvSyms(armOvDir, arm9ovDir, arm7ovDir)) return 1;
	}
	else
	{
		// -------- Regenerate XML

		// Load minimalistic header.bin
		auto ndsHeader = std::make_unique<NDSHeader>();
		std::ifstream headerFile(headerFilePath, std::ios::binary);
		if (!headerFile.is_open())
		{
			std::cout << "Failed to load \"" << headerFilePath.string() << "\"" << std::endl;
			return 1;
		}
		headerFile.read(reinterpret_cast<char*>(ndsHeader.get()), sizeof(NDSHeader));
		headerFile.close();

		// Rewrite missing XML elements
		if (!restoreOvt(romInfoNode, true, arm9ovtPath)) return 1;
		if (!restoreOvt(romInfoNode, false, arm7ovtPath)) return 1;

		// Save the new XML
		doc.save_file(xmlPathStr.c_str(), "  ");

		// Destroy header.bin
		fs::remove(headerFilePath);

		// Destroy arm9ovt.bin and arm7ovt.bin
		if (fs::exists(arm9ovtPath))
			fs::remove(arm9ovtPath);
		if (fs::exists(arm7ovtPath))
			fs::remove(arm7ovtPath);

		// Destroy symlinks
		fs::remove_all(arm9ovDir);
		fs::remove_all(arm7ovDir);
	}

	return 0;
}
