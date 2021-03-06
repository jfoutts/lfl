############################################
#
# How to build libarchive_test
#
############################################
IF(ENABLE_TEST)
  FOREACH (_src ${libarchive_SOURCES})
    LIST(APPEND parent_libarchive_SOURCES "../${_src}")
  ENDFOREACH(_src)

  SET(libarchive_test_SOURCES
    ${parent_libarchive_SOURCES}
    main.c
    read_open_memory.c
    test.h
    test_acl_basic.c
    test_acl_freebsd.c
    test_acl_pax.c
    test_archive_api_feature.c
    test_bad_fd.c
    test_compat_bzip2.c
    test_compat_gtar.c
    test_compat_gzip.c
    test_compat_tar_hardlink.c
    test_compat_xz.c
    test_compat_zip.c
    test_empty_write.c
    test_entry.c
    test_entry_strmode.c
    test_extattr_freebsd.c
    test_fuzz.c
    test_link_resolver.c
    test_open_fd.c
    test_open_file.c
    test_open_filename.c
    test_pax_filename_encoding.c
    test_read_compress_program.c
    test_read_data_large.c
    test_read_disk.c
    test_read_disk_entry_from_file.c
    test_read_extract.c
    test_read_file_nonexistent.c
    test_read_format_ar.c
    test_read_format_cpio_bin.c
    test_read_format_cpio_bin_Z.c
    test_read_format_cpio_bin_bz2.c
    test_read_format_cpio_bin_gz.c
    test_read_format_cpio_bin_xz.c
    test_read_format_cpio_odc.c
    test_read_format_cpio_svr4_gzip.c
    test_read_format_cpio_svr4c_Z.c
    test_read_format_empty.c
    test_read_format_gtar_gz.c
    test_read_format_gtar_lzma.c
    test_read_format_gtar_sparse.c
    test_read_format_iso_gz.c
    test_read_format_isojoliet_bz2.c
    test_read_format_isorr_bz2.c
    test_read_format_mtree.c
    test_read_format_pax_bz2.c
    test_read_format_tar.c
    test_read_format_tar_empty_filename.c
    test_read_format_tbz.c
    test_read_format_tgz.c
    test_read_format_txz.c
    test_read_format_tz.c
    test_read_format_zip.c
    test_read_large.c
    test_read_pax_truncated.c
    test_read_position.c
    test_read_truncated.c
    test_tar_filenames.c
    test_tar_large.c
    test_ustar_filenames.c
    test_write_compress.c
    test_write_compress_bzip2.c
    test_write_compress_gzip.c
    test_write_compress_lzma.c
    test_write_compress_program.c
    test_write_compress_xz.c
    test_write_disk.c
    test_write_disk_failures.c
    test_write_disk_hardlink.c
    test_write_disk_perms.c
    test_write_disk_secure.c
    test_write_disk_sparse.c
    test_write_disk_times.c
    test_write_format_ar.c
    test_write_format_cpio.c
    test_write_format_cpio_empty.c
    test_write_format_cpio_odc.c
    test_write_format_cpio_newc.c
    test_write_format_mtree.c
    test_write_format_pax.c
    test_write_format_shar_empty.c
    test_write_format_tar.c
    test_write_format_tar_empty.c
    test_write_format_tar_ustar.c
    test_write_open_memory.c
  )

  #
  # Generate the list.h
  #
  GENERATE_LIST_H(${CMAKE_CURRENT_BINARY_DIR}/list.h
    ${CMAKE_CURRENT_LIST_FILE} ${libarchive_test_SOURCES})
  SET_PROPERTY(DIRECTORY APPEND PROPERTY INCLUDE_DIRECTORIES
    ${CMAKE_CURRENT_BINARY_DIR})
  #
  # Register target
  #
  ADD_EXECUTABLE(libarchive_test ${libarchive_test_SOURCES})
  TARGET_LINK_LIBRARIES(libarchive_test ${ADDITIONAL_LIBS})
  SET_PROPERTY(TARGET libarchive_test PROPERTY COMPILE_DEFINITIONS
    LIBARCHIVE_STATIC LIST_H)
  SET(num 0)
  FOREACH(test ${libarchive_test_SOURCES})
    IF(test MATCHES "^test_[^/]+[.]c$")
      STRING(REGEX REPLACE "^(test_[^/]+)[.]c$" "\\1" testname ${test})
      ADD_TEST("libarchive_${testname}" libarchive_test
        -q -r ${CMAKE_CURRENT_SOURCE_DIR} ${num})
      MATH(EXPR num "${num} + 1")
    ENDIF(test MATCHES "^test_[^/]+[.]c$")
  ENDFOREACH(test)
ENDIF(ENABLE_TEST)

