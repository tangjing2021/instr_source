{ pkgs ? import <nixpkgs> {} }:

let
  # 从 GitHub 拉取插件源码
  instrSource = pkgs.fetchFromGitHub {
    owner = "tangjing2021";
    repo = "instr_source";
    rev = "main";
    # 可以先填一个假的 sha256，nix-shell 会提示真实 hash
    sha256 = "sha256-6l3polCZjNx2DjKk78vi8lIgVyLmqqbelDItWZIGLiI=";
  };

  # 使用支持插件 API v4 的 QEMU
  qemuWithPluginSupport = pkgs.qemu.overrideAttrs (old: {
    configureFlags = (old.configureFlags or []) ++ [
      "--target-list=riscv64-linux-user"
      "--enable-plugins"
      "--enable-linux-user"
    ];
  });

  # Capstone: 启用 RISC-V B 扩展，仅构建 riscv 支持
  capstoneRiscv = pkgs.capstone.overrideAttrs (old: {
    cmakeFlags = (old.cmakeFlags or []) ++ [
      "-DCAPSTONE_RISCV64_B_EXTENSION=ON"
      "-DCAPSTONE_ARCHITECTURE_DEFAULT=OFF"
      "-DCAPSTONE_RISCV_SUPPORT=ON"
      # 禁用其他架构
      "-DCAPSTONE_X86_SUPPORT=OFF"
      "-DCAPSTONE_ARM_SUPPORT=OFF"
      "-DCAPSTONE_ARM64_SUPPORT=OFF"
      "-DCAPSTONE_MIPS_SUPPORT=OFF"
      "-DCAPSTONE_PPC_SUPPORT=OFF"
      "-DCAPSTONE_SPARC_SUPPORT=OFF"
      "-DCAPSTONE_SYSZ_SUPPORT=OFF"
      "-DCAPSTONE_XCORE_SUPPORT=OFF"
      "-DCAPSTONE_M68K_SUPPORT=OFF"
      "-DCAPSTONE_TMS320C64X_SUPPORT=OFF"
    ];
  });

  # 插件构建脚本，使用从 GitHub 拉下来的源文件
  buildPlugin = pkgs.writeShellScriptBin "build-plugin" ''
    set -euo pipefail

    # 源文件路径
    PLUGIN_SRC="${instrSource}/parsec_inst_plugin.c"
    cat $PLUGIN_SRC
    # 设置编译环境
    export C_INCLUDE_PATH="${qemuWithPluginSupport}/include:${capstoneRiscv}/include:${pkgs.glib.dev}/include/glib-2.0:${pkgs.glib.out}/lib/glib-2.0/include"
    export LIBRARY_PATH="${capstoneRiscv}/lib:${pkgs.glib.out}/lib"

    echo "编译 QEMU 插件..."

    # 编译插件
    ${pkgs.gcc}/bin/gcc -shared -fPIC -o parsec_ins_plugin.so \
      -I${qemuWithPluginSupport}/include \
      -I${capstoneRiscv}/include \
      -I${pkgs.glib.dev}/include/glib-2.0 \
      -I${pkgs.glib.out}/lib/glib-2.0/include \
      "$PLUGIN_SRC" \
      -lcapstone -lglib-2.0

    # 验证插件是否编译成功
    if [ -f parsec_ins_plugin.so ]; then
      echo "✅ 插件编译成功: parsec_ins_plugin.so"

      # 检查插件兼容性
      PLUGIN_API=$(${qemuWithPluginSupport}/bin/qemu-riscv64 -plugin help 2>&1 | grep "API version" | awk '{print $4}')
      echo "QEMU 插件 API 版本: $PLUGIN_API"
    else
      echo "❌ 插件编译失败"
      exit 1
    fi
  '';

in
pkgs.mkShell {
  buildInputs = [
    # Python3 解释器
    pkgs.python3
    # Matplotlib Python 包
    pkgs.python3Packages.matplotlib

    # QEMU riscv64-user + plugin 支持
    qemuWithPluginSupport

    # Capstone riscv 支持
    capstoneRiscv

    # 插件编译依赖
    pkgs.gcc
    pkgs.glib
    pkgs.pkg-config

    # 插件构建脚本
    buildPlugin

    # 可选：riscv64 交叉 gcc
    pkgs.pkgsCross.riscv64.buildPackages.gcc
  ];

  shellHook = ''
    echo "进入开发环境："
    echo "- Python: $(python3 --version)"
    echo "- Matplotlib: $(python3 -c 'import matplotlib; print(matplotlib.__version__)' 2>/dev/null)"
    echo "- QEMU: $(qemu-riscv64 --version | head -n1)"
    echo "- Capstone: $(cstool --version 2>&1 | head -n1)"

    # 检查插件 API 版本
    PLUGIN_API=$(${qemuWithPluginSupport}/bin/qemu-riscv64 -plugin help 2>&1 | grep "API version" | awk '{print $4}')
    echo "- QEMU 插件 API 版本: $PLUGIN_API"

    echo ""
    echo "要编译插件，请运行: build-plugin"
    echo "然后使用以下命令运行程序:"
    echo "  qemu-riscv64 -plugin ./parsec_ins_plugin.so /path/to/program"
  '';
}
