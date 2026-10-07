# OpenEmbedded layer

This `wrynose` layer provides `onekvm-video-soph-mmf-modules` for NanoKVM,
including its module loaders, systemd units, module configuration and HDMI
prepare helper. It depends on OE-Core and `onekvm-bsp` from `onekvm-distro`.
The distro retains the kernel, codec firmware and boot-tuning overrides.

Build through `onekvm-distro`; do not cross-compile this repository directly:

```sh
ONEKVM_COMPONENT_LOCAL_MIRROR=1 ./scripts/kas.sh shell kas/nanokvm-sd.yml \
  -c 'bitbake onekvm-video-soph-mmf-modules'
```

`kas/components.lock` pins the layer and driver source together through
`ONEKVM_COMPONENT_SRCREV`. The initial migration preserves the previously
pinned driver code, package version, dependencies and service behavior.

To use current driver code and uncommitted recipe or loader changes:

```sh
ONEKVM_COMPONENT_LOCAL_MIRROR=1 ONEKVM_DEBUG_WORKTREES=osdrv-sg200x \
  ./scripts/kas.sh shell kas/nanokvm-sd.yml \
  -c 'bitbake -c package_write_ipk onekvm-video-soph-mmf-modules'
```

This loads the working tree layer and sets `EXTERNALSRC` for the modules only.
Build outputs remain in `onekvm-distro/.oe/`. Deployment follows the target
device's authorization; recipe migration does not require deploying modules.
