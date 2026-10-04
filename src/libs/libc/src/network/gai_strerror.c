#include <netdb.h>

const char *gai_strerror(int code) {
  switch (code) {
  case EAI_NONAME: return "Name or service not known";
  case EAI_MEMORY: return "Memory allocation failure";
  case EAI_FAMILY: return "ai_family not supported";
  case EAI_SOCKTYPE: return "ai_socktype not supported";
  case EAI_SERVICE: return "Servname not supported for ai_socktype";
  case EAI_SYSTEM: return "System error";
  default: return "Unknown error";
  }
}
