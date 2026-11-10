package=native_rust
# Official Rust release components, pinned by the digests published on
# static.rust-lang.org. The rustc and cargo components match the build machine;
# rust-std is fetched for every release target so that `make download` outside
# the Guix container provides everything a cross build needs offline.
$(package)_version=1.96.0
$(package)_download_path=https://static.rust-lang.org/dist
$(package)_rust_build=$(sv2_rust_build)
$(package)_rust_host=$(sv2_rust_host)
$(package)_file_name=rustc-$($(package)_version)-$($(package)_rust_build).tar.xz
$(package)_sha256_hash=$($(package)_rustc_$($(package)_rust_build)_sha256_hash)

$(package)_rustc_x86_64-unknown-linux-gnu_sha256_hash=7d7fa1d0cfb0fab71a956bb78f41107202c17f30ab56c45288e869a37fd9633d
$(package)_rustc_aarch64-unknown-linux-gnu_sha256_hash=76b1a6e8dd1636e364d4bbba685485ff44eee5ff6434add089bab4c703c7e19d
$(package)_rustc_aarch64-apple-darwin_sha256_hash=1bb7b0bad1d2a42fc4173ede6dd460de2774fc1858a8369329d3e081e4e3426c
$(package)_rustc_x86_64-apple-darwin_sha256_hash=f503815fe9e8cf6d654f751532932b6a9b13b8615a40fc6dfb9760a18cf595a1

$(package)_cargo_file_name=cargo-$($(package)_version)-$($(package)_rust_build).tar.xz
$(package)_cargo_sha256_hash=$($(package)_cargo_$($(package)_rust_build)_sha256_hash)
$(package)_cargo_x86_64-unknown-linux-gnu_sha256_hash=dee75c3c8f9f600ad75bc0c93249e767d3047845a4dd668327ce43ab039ba266
$(package)_cargo_aarch64-unknown-linux-gnu_sha256_hash=09ea03e74aa94e07db7bc00bd2ec1ad86d90a7348c89fde3909a8922543b949f
$(package)_cargo_aarch64-apple-darwin_sha256_hash=c042858192b7b6d66fe59b3bbbbd0f6e3cac6e8a478dc4cc091cde9eddea3c8b
$(package)_cargo_x86_64-apple-darwin_sha256_hash=23390ad69f74f3464774f17058f803e19cf45a9a11ee725b7a37e96c549f1243

$(package)_std_targets=x86_64-unknown-linux-gnu aarch64-unknown-linux-gnu armv7-unknown-linux-gnueabihf \
  riscv64gc-unknown-linux-gnu powerpc64-unknown-linux-gnu powerpc64le-unknown-linux-gnu \
  x86_64-pc-windows-gnu x86_64-apple-darwin aarch64-apple-darwin
$(package)_std_x86_64-unknown-linux-gnu_sha256_hash=c09c7c646248f14f473f5f7a029af15ee57c3a9f9bc93dfa72d9621938586b82
$(package)_std_aarch64-unknown-linux-gnu_sha256_hash=538e85452709687797d990579a491ff9b02f8bffba4a5d54cfa945e28868053e
$(package)_std_armv7-unknown-linux-gnueabihf_sha256_hash=460140e9be152e2b5a125d45a8cd11d9d956cb0e6cf8bc30c45ac009b4bfc896
$(package)_std_riscv64gc-unknown-linux-gnu_sha256_hash=595e4a980dccb7571441f330782cad6cd23d64f331ed453de57dd386665f5615
$(package)_std_powerpc64-unknown-linux-gnu_sha256_hash=cf273029a7a8dba56fe4fd517ae4c8bd862a3e27c56369a8b9d6411c8d74085d
$(package)_std_powerpc64le-unknown-linux-gnu_sha256_hash=347ff4e5dd463849f21391b9db90093dee33578d98f30b42bbb78b4777921747
$(package)_std_x86_64-pc-windows-gnu_sha256_hash=4502f0fdac8151b874723acb771cc5dca9a33df9f12a40081947909058bb46dc
$(package)_std_x86_64-apple-darwin_sha256_hash=afabf23aff5bf6d27dba9608a7c7bec349bf9fda9c3e37983dd5cc44c9afbcca
$(package)_std_aarch64-apple-darwin_sha256_hash=439c4f71060b913e00db3a2e01340b2da0aa49978b843e36871f3250267c63f8
$(package)_std_file=rust-std-$($(package)_version)-$(1).tar.xz

$(package)_extra_sources=$($(package)_cargo_file_name) $(foreach t,$($(package)_std_targets),$(call $(package)_std_file,$(t)))

define $(package)_fetch_cmds
$(call fetch_file,$(package),$($(package)_download_path),$($(package)_file_name),$($(package)_file_name),$($(package)_sha256_hash)) && \
$(call fetch_file,$(package),$($(package)_download_path),$($(package)_cargo_file_name),$($(package)_cargo_file_name),$($(package)_cargo_sha256_hash)) && \
$(foreach t,$($(package)_std_targets),$(call fetch_file,$(package),$($(package)_download_path),$(call $(package)_std_file,$(t)),$(call $(package)_std_file,$(t)),$($(package)_std_$(t)_sha256_hash)) && ) true
endef

# Install the build-machine std (build scripts and proc macros) and, when
# cross compiling, the host std.
$(package)_install_std_targets=$(sort $($(package)_rust_build) $($(package)_rust_host))

define $(package)_extract_cmds
  mkdir -p $$($(package)_extract_dir) && \
  echo "$($(package)_sha256_hash)  $($(package)_source_dir)/$($(package)_file_name)" > $$($(package)_extract_dir)/.$($(package)_file_name).hash && \
  echo "$($(package)_cargo_sha256_hash)  $($(package)_source_dir)/$($(package)_cargo_file_name)" >> $$($(package)_extract_dir)/.$($(package)_file_name).hash && \
  $(foreach t,$($(package)_install_std_targets),echo "$($(package)_std_$(t)_sha256_hash)  $($(package)_source_dir)/$(call $(package)_std_file,$(t))" >> $$($(package)_extract_dir)/.$($(package)_file_name).hash && ) \
  $(build_SHA256SUM) -c $$($(package)_extract_dir)/.$($(package)_file_name).hash && \
  mkdir -p rustc cargo std && \
  $(build_TAR) --no-same-owner --strip-components=2 -C rustc -xf $($(package)_source_dir)/$($(package)_file_name) rustc-$($(package)_version)-$($(package)_rust_build)/rustc && \
  $(build_TAR) --no-same-owner --strip-components=2 -C cargo -xf $($(package)_source_dir)/$($(package)_cargo_file_name) cargo-$($(package)_version)-$($(package)_rust_build)/cargo && \
  $(foreach t,$($(package)_install_std_targets),$(build_TAR) --no-same-owner --strip-components=2 -C std -xf $($(package)_source_dir)/$(call $(package)_std_file,$(t)) rust-std-$($(package)_version)-$(t)/rust-std-$(t) && ) true
endef

# The official Linux binaries expect an FHS dynamic loader. Inside the Guix
# container there is none, so point them at the loader and libraries of the
# Guix build environment.
ifeq ($(build_os),linux)
$(package)_loader_x86_64=ld-linux-x86-64.so.2
$(package)_loader_aarch64=ld-linux-aarch64.so.1
define $(package)_preprocess_cmds
  if test -n "$(GUIX_ENVIRONMENT)"; then \
    for f in $$$$(find rustc cargo -type f \( -path '*/bin/*' -o -name '*.so' \)); do \
      if head -c 4 "$$$$f" | grep -q ELF; then \
        if test -x "$$$$f" && ! case "$$$$f" in *.so) true;; *) false;; esac; then \
          patchelf --set-interpreter "$(GUIX_ENVIRONMENT)/lib/$($(package)_loader_$(build_arch))" "$$$$f"; \
        fi; \
        patchelf --set-rpath '$$$$ORIGIN/../lib:$(GUIX_ENVIRONMENT)/lib' "$$$$f"; \
      fi; \
    done; \
  fi
endef
endif

define $(package)_stage_cmds
  mkdir -p $($(package)_staging_prefix_dir) && \
  cp -R rustc/. cargo/. std/. $($(package)_staging_prefix_dir)/ && \
  rm -rf $($(package)_staging_prefix_dir)/share/doc $($(package)_staging_prefix_dir)/share/man \
         $($(package)_staging_prefix_dir)/share/zsh $($(package)_staging_prefix_dir)/etc \
         $($(package)_staging_prefix_dir)/lib/rustlib/manifest-* \
         $($(package)_staging_prefix_dir)/lib/rustlib/components \
         $($(package)_staging_prefix_dir)/lib/rustlib/rust-installer-version \
         $($(package)_staging_prefix_dir)/lib/rustlib/install.log \
         $($(package)_staging_prefix_dir)/lib/rustlib/uninstall.sh
endef
