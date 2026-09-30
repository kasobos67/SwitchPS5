# Preserve the upstream container; repair its throwing-constructor contract.
file(READ "${SLOT_VECTOR_INPUT}" slot_source)
set(old_insert "[[nodiscard]] SlotId insert(Args&&... args) noexcept {\n        const u32 index = FreeValueIndex();\n        new (&values[index].object) T(std::forward<Args>(args)...);")
string(FIND "${slot_source}" "${old_insert}" insert_offset)
if(insert_offset LESS 0)
    message(FATAL_ERROR "Pinned SlotVector insertion changed")
endif()
string(REPLACE "${old_insert}"
    "[[nodiscard]] SlotId insert(Args&&... args) {\n        const u32 index = FreeValueIndex();\n        try {\n            new (&values[index].object) T(std::forward<Args>(args)...);\n        } catch (...) {\n            free_list.push_back(index); // Restores the popped slot without allocating.\n            throw;\n        }"
    slot_source "${slot_source}")
file(WRITE "${SLOT_VECTOR_OUTPUT}.in" "${slot_source}")
configure_file("${SLOT_VECTOR_OUTPUT}.in" "${SLOT_VECTOR_OUTPUT}" COPYONLY)
