AC_DEFUN([AC_FLDIGI_JPEG], [
    AC_CHECK_HEADER([jpeglib.h], [], [AC_MSG_FAILURE([could not find jpeglib.h])])
    AC_CHECK_LIB([jpeg], [jpeg_abort], [], [AC_MSG_FAILURE([could not find -ljpeg])])
])
