# Unmodified libmultiprocess snapshot shipped in Bitcoin Core v32.1.
package=native_libmultiprocess
$(package)_version=f0de87f040621468b956f01f526a43a010310501
$(package)_download_path=https://github.com/bitcoin/bitcoin/archive
$(package)_file_name=$($(package)_version).tar.gz
$(package)_sha256_hash=2bd6df218ad696e1a69530131a3fb67468c00d579f6332dda3fe1d5e5e007a25
$(package)_dependencies=native_capnp

define $(package)_config_cmds
  $($(package)_cmake) src/ipc/libmultiprocess
endef

define $(package)_build_cmds
  $(MAKE)
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install-bin
endef
