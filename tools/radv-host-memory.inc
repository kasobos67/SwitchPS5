bool
radv_ps5_memory_grant_gpu(void *address, uint64_t bytes)
{
   const uint32_t required = PS5_KERNEL_PROT_CPU_READ | PS5_KERNEL_PROT_CPU_WRITE |
                             PS5_KERNEL_PROT_GPU_READ | PS5_KERNEL_PROT_GPU_WRITE;
   const uintptr_t begin = (uintptr_t)address;
   if (!begin || !bytes || bytes > UINTPTR_MAX - begin)
      return false;
   const uintptr_t limit = begin + bytes;
   uintptr_t cursor = begin;
   /* VideoOut already registered these GPU-visible pages. Do not change their
    * protection again: validate the entire imported range, including subviews. */
   while (cursor < limit) {
      void *start = NULL, *end = NULL;
      uint32_t protection = 0;
      if (sceKernelQueryMemoryProtection((void *)cursor, &start, &end, &protection) != 0 ||
          (uintptr_t)start > cursor || (uintptr_t)end <= cursor ||
          (protection & required) != required)
         break;
      cursor = (uintptr_t)end;
   }
   if (cursor >= limit)
      return true;
   const int32_t result = sceKernelMprotect(address, bytes, required);
   if (result != 0)
      fprintf(stderr, "radv/ps5: host GPU protection failed address=%p bytes=%llu result=0x%08x\n",
              address, (unsigned long long)bytes, (unsigned)result);
   return result == 0;
}
