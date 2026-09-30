# -*- coding: utf-8 -*-
"""
kht 一键构建 + 部署脚本
工具链：Qt 6.8.3 (llvm-mingw) + LLVM-MinGW 22.17 + CMake 3.30 + Ninja
用法：python build.py
"""
import subprocess, shutil, sys, os

# ---- 工具链路径（如安装位置不同请修改这里）----
QT_DIR = r"D:\Qt\6.8.3\llvm-mingw_64"
MINGW_DIR = r"D:\Qt\Tools\llvm-mingw2217_64"
CMAKE = r"D:\build-tools\Tools\CMake_64\bin\cmake.exe"
NINJA = r"D:\build-tools\Tools\Ninja\ninja.exe"

PROJECT_DIR = os.path.dirname(os.path.abspath(__file__))
BUILD_DIR = os.path.join(PROJECT_DIR, "build")
RELEASE_DIR = os.path.join(PROJECT_DIR, "release")
DB_SRC = os.path.join(PROJECT_DIR, "jcr.db")

env = os.environ.copy()
env["PATH"] = os.path.join(MINGW_DIR, "bin") + ";" + env.get("PATH", "")


def run(cmd):
    print(">>", " ".join(cmd) if isinstance(cmd, list) else cmd)
    r = subprocess.run(cmd, shell=isinstance(cmd, str), env=env)
    if r.returncode != 0:
        print("!! 失败，退出码", r.returncode)
        sys.exit(1)


def main():
    # 1. 配置
    print("[1/4] 配置 CMake ...")
    run([CMAKE, "-G", "Ninja",
         "-DCMAKE_MAKE_PROGRAM=" + NINJA,
         "-DCMAKE_PREFIX_PATH=" + QT_DIR,
         "-DCMAKE_C_COMPILER=" + os.path.join(MINGW_DIR, "bin", "clang.exe"),
         "-DCMAKE_CXX_COMPILER=" + os.path.join(MINGW_DIR, "bin", "clang++.exe"),
         "-DCMAKE_BUILD_TYPE=Release",
         "-S", PROJECT_DIR, "-B", BUILD_DIR])

    # 2. 编译
    print("[2/4] 编译 ...")
    run([CMAKE, "--build", BUILD_DIR, "--parallel"])

    # 3. 部署
    print("[3/4] 部署依赖 ...")
    os.makedirs(RELEASE_DIR, exist_ok=True)
    shutil.copy(os.path.join(BUILD_DIR, "kht.exe"), RELEASE_DIR)
    shutil.copy(DB_SRC, RELEASE_DIR)
    run([os.path.join(QT_DIR, "bin", "windeployqt.exe"),
         os.path.join(RELEASE_DIR, "kht.exe"),
         "--no-translations", "--no-system-d3d-compiler", "--no-opengl-sw",
         "--skip-plugin-types", "generic,networkinformation,tls"])
    # llvm-mingw 运行时（windeployqt 不部署）
    for dll in ["libc++.dll", "libunwind.dll"]:
        shutil.copy(os.path.join(MINGW_DIR, "bin", dll), RELEASE_DIR)
    # 清理冗余插件（仅用 SQLite + JPEG/SVG）
    for p in ["sqldrivers/qsqlmimer.dll", "sqldrivers/qsqlodbc.dll", "sqldrivers/qsqlpsql.dll",
              "imageformats/qgif.dll", "imageformats/qico.dll"]:
        fp = os.path.join(RELEASE_DIR, p)
        if os.path.exists(fp):
            os.remove(fp)

    print("[4/4] 完成！发布目录:", RELEASE_DIR)


if __name__ == "__main__":
    main()
