set(ata_source_dir "${PROJECT_SOURCE_DIR}/pcsx2/DEV9/ATA")
add_pcsx2_test(dev9_ata_test
	ata_test_bridge.cpp
	ata_image_tests.cpp
	"${ata_source_dir}/ATA_Info.cpp"
	"${ata_source_dir}/ATA_State.cpp"
	"${ata_source_dir}/ATA_Transfer.cpp"
	"${ata_source_dir}/Commands/ATA_Command.cpp"
	"${ata_source_dir}/Commands/ATA_CmdDMA.cpp"
	"${ata_source_dir}/Commands/ATA_CmdExecuteDeviceDiag.cpp"
	"${ata_source_dir}/Commands/ATA_CmdNoData.cpp"
	"${ata_source_dir}/Commands/ATA_CmdPIOData.cpp"
	"${ata_source_dir}/Commands/ATA_CmdSMART.cpp"
	"${ata_source_dir}/Commands/ATA_SCE.cpp"
)

# Do not link PCSX2 or PCSX2_FLAGS: this target intentionally substitutes only
# the DEV9 bridge, and does not pull in the emulator, GS, network or Qt objects.
target_include_directories(dev9_ata_test BEFORE PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/bridge"
	"${PROJECT_SOURCE_DIR}/pcsx2"
)
target_precompile_headers(dev9_ata_test PRIVATE "${PROJECT_SOURCE_DIR}/pcsx2/PrecompiledHeader.h")
target_link_libraries(dev9_ata_test PRIVATE common)
target_compile_features(dev9_ata_test PRIVATE cxx_std_17)

if(WIN32)
	target_compile_definitions(dev9_ata_test PRIVATE WINVER=0x0A00 _WIN32_WINNT=0x0A00 WIN32_LEAN_AND_MEAN)
endif()
