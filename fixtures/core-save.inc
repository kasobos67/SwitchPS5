// SPDX-License-Identifier: GPL-3.0-or-later
// Ordinary account-save IPC. Test-only title/user identity, inside the isolated NAND.
#ifdef EDEN_GUEST_SAVE_PHASE
static u32 open_test_save(u32 fs) {
    // SpaceId byte + alignment, then the 0x40-byte SaveDataAttribute.
    const u64 args[9] = {1, 0x010000000000e001ULL,
        0x1122334455667788ULL, 0x8877665544332211ULL, 0, 1, 0, 0, 0};
    u32 save;
    ipc(fs, 51, 0, 0, 0, 0, args, 9, &save);
    return save;
}
static void check_save(u32 fs) {
    const char path[] = "/eden-save-persistence.bin";
    for (u32 i = 0; i < sizeof(file_path); ++i) file_path[i] = 0;
    for (u32 i = 0; i < sizeof(path); ++i) file_path[i] = path[i];
    u32 save, file;
#if EDEN_GUEST_SAVE_PHASE == 1
    stage = 50;
    // CreationInfo (0x40), Attribute (0x40), uid (0x10), all zero-padded.
    const u64 create[18] = {0x10000, 0x10000, 0x4000, 0, 1ULL << 32, 0, 0, 0,
        0x010000000000e001ULL, 0x1122334455667788ULL, 0x8877665544332211ULL,
        0, 1, 0, 0, 0, 0, 0};
    ipc(fs, 22, 0, 0, 0, 0, create, 18, 0);
    save = open_test_save(fs);
    stage = 51;
    const u64 create_file[] = {0, sizeof(written)};
    ipc(save, 0, 0, 1, file_path, sizeof(file_path), create_file, 2, 0);
    const u64 write_mode[] = {2};
    ipc(save, 8, 0, 1, file_path, sizeof(file_path), write_mode, 1, &file);
    for (u32 i = 0; i < sizeof(written); ++i) written[i] = (unsigned char)(i * 29 + 7);
    const u64 write_args[] = {1, 0, sizeof(written)};
    ipc(file, 1, 0, 2, written, sizeof(written), write_args, 3, 0);
    ipc(file, 2, 0, 0, 0, 0, 0, 0, 0);
    close(file);
    ipc(save, 10, 0, 0, 0, 0, 0, 0, 0); // Commit: currently a success stub in Eden.
    close(save);
    PRINT("EDEN_GUEST_SAVE_WRITE_PASS");
#endif
    stage = 52;
    save = open_test_save(fs);
    const u64 read_mode[] = {1};
    ipc(save, 8, 0, 1, file_path, sizeof(file_path), read_mode, 1, &file);
    stage = 53;
    require(*ipc(file, 4, 0, 0, 0, 0, 0, 0, 0) == sizeof(read_back));
    const u64 read_args[] = {0, 0, sizeof(read_back)};
    require(*ipc(file, 0, 0, 3, read_back, sizeof(read_back), read_args, 3, 0) == sizeof(read_back));
    stage = 54;
    for (u32 i = 0; i < sizeof(read_back); ++i)
        require(read_back[i] == (unsigned char)(i * 29 + 7));
    close(file); close(save);
    PRINT("EDEN_GUEST_SAVE_READ_PASS");
}
#endif
