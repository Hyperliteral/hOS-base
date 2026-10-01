#include <executable.h>
#include <vfs.h>

bool executable_is_header(const void *image, size_t size) {
  if (size < 4)
    return false;
#if CONFIG_EXEC_ELF
  const uint8_t *bytes = (const uint8_t *)image;
  if (bytes[0] == ELFMAG0 && bytes[1] == ELFMAG1 && bytes[2] == ELFMAG2 &&
      bytes[3] == ELFMAG3)
    return true;
#endif
#if CONFIG_MHO_EXEC
  return macho_is_header(bytes, size);
#else
  return false;
#endif
}

Task *executableExecute(char *filepath, uint32_t argc, char **argv,
                        uint32_t envc, char **envv, bool startup) {
#if !CONFIG_EXEC
  debugf(
      "[executable] Error: No Executable Format is enabled in this Build.\n");
  return 0;
#endif

  OpenFile *file = fsKernelOpen(filepath, O_RDONLY, 0);
  if (!file)
    return 0;

  uint8_t magic[4];
  size_t  read = fsRead(file, magic, sizeof(magic));
  fsKernelClose(file);
  if (read != sizeof(magic))
    return 0;

  debugf("executing..");

#if CONFIG_EXEC_ELF
  if (magic[0] == ELFMAG0 && magic[1] == ELFMAG1 && magic[2] == ELFMAG2 &&
      magic[3] == ELFMAG3)
    return elfExecute(filepath, argc, argv, envc, envv, startup);
#endif

#if CONFIG_MHO_EXEC
  if (macho_is_header(magic, sizeof(magic)))
    return machoExecute(filepath, argc, argv, envc, envv, startup);
#endif

  return 0;
}
