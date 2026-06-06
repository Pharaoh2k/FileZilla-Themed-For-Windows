#ifdef FZ_WINDOWS
  // IE 9 or higher
  #ifndef _WIN32_IE
    #define _WIN32_IE 0x0900
  #elif _WIN32_IE <= 0x0900
    #undef _WIN32_IE
    #define _WIN32_IE 0x0900
  #endif

  // Windows 7 or higher
  #ifndef _WIN32_WINNT
    #define _WIN32_WINNT 0x0601
  #elif _WIN32_WINNT < 0x0601
    #undef _WIN32_WINNT
    #define _WIN32_WINNT 0x0601
  #endif

  // Windows 7 or higher
  #ifndef WINVER
    #define WINVER 0x0601
  #elif WINVER < 0x0601
    #undef WINVER
    #define WINVER 0x0601
  #endif
#endif
