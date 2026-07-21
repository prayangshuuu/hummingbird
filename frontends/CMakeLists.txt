# frontends/ — user-facing binaries. These are the ONLY components allowed to
# call exit() (ADR DD-011); the library returns hb_status instead.
add_subdirectory(cli)
add_subdirectory(server)
