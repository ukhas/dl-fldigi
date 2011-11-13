AC_DEFUN([AC_FLDIGI_JSONCPP], [
    AC_LANG_PUSH([C++])
    LIBS="$LIBS -ljsoncpp"
    AC_MSG_CHECKING([for libjsoncpp])
    AC_LINK_IFELSE(
      [AC_LANG_PROGRAM([#include <json/json.h>],
        [Json::Value value(123); value = "test";])],
      [AC_MSG_RESULT([yes])],
      [AC_MSG_FAILURE([libjsoncpp is not installed.])])
    AC_LANG_POP([C++])
])
