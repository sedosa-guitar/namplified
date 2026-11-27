set(archive_url "https://github.com/astral-sh/python-build-standalone/releases/download/20261110/cpython-3.11.18%2B20261110-aarch64-apple-darwin-install_only_stripped.tar.gz")
set(archive_hash "3afb0567b2a2b30b3a7341bd143cb4e8a69f3d8e95d4641218eaa45f0fabbd07")

if (EXISTS "${ARCHIVE}")
    file(SHA256 "${ARCHIVE}" existing_hash)
    if (existing_hash STREQUAL archive_hash)
        return()
    endif()
endif()

file(DOWNLOAD "${archive_url}" "${ARCHIVE}.part"
     EXPECTED_HASH "SHA256=${archive_hash}" TLS_VERIFY ON TIMEOUT 180)
file(RENAME "${ARCHIVE}.part" "${ARCHIVE}")
