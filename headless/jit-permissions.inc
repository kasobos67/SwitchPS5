// Included in the generated Unix BlockOfCode implementation, inside its namespace.
void BlockOfCode::EnableWriting() {
    if (writing) return;
    ASSERT(getSize() == protection_end);
    if (hasWritableAlias()) { writing = true; return; }
    static const size_t page = sysconf(_SC_PAGESIZE);
    write_begin = getSize() & ~(page - 1);
    if (write_begin < executable_end)
        ProtectMemory(getCode() + write_begin, executable_end - write_begin, false);
    writing = true;
}

void BlockOfCode::DisableWriting() {
    if (!writing) return;
    protection_end = std::max(protection_end, getSize());
    if (hasWritableAlias()) { writing = false; return; }
    static const size_t page = sysconf(_SC_PAGESIZE);
    const size_t end = (protection_end + page - 1) & ~(page - 1);
    // Publish adjacent patches together, including the append tail when adjacent.
    std::sort(patch_pages.begin(), patch_pages.end());
    size_t begin = end, limit = end;
    for (size_t offset : patch_pages) {
        if (offset != limit) {
            if (limit > begin) ProtectMemory(getCode() + begin, limit - begin, true);
            begin = offset;
        }
        limit = offset + page;
    }
    if (limit == write_begin) limit = end;
    else {
        if (limit > begin) ProtectMemory(getCode() + begin, limit - begin, true);
        begin = write_begin;
        limit = end;
    }
    if (limit > begin) ProtectMemory(getCode() + begin, limit - begin, true);
    patch_pages.clear();
    executable_end = end;
    writing = false;
}

void BlockOfCode::ClearCache() {
    ASSERT(prelude_complete);
    const bool was_writing = writing;
    DisableWriting();
    static const size_t page = sysconf(_SC_PAGESIZE);
    const size_t begin = reinterpret_cast<const u8*>(code_begin) - getCode();
    if (hasWritableAlias()) {
        protection_end = begin;
        setSize(begin);
        writing = was_writing;
        return;
    }
    write_begin = begin & ~(page - 1);
    if (write_begin < executable_end)
        ProtectMemory(getCode() + write_begin, executable_end - write_begin, false);
    executable_end = write_begin;
    protection_end = begin;
    setSize(begin);
    writing = true;
    if (!was_writing) DisableWriting();
}
