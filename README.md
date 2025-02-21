# StrangeLinux
一系列和操作系统有关的小作业。

## 环境
- 基础系统环境（按道理来讲不应该有影响，仅供debug）（截至项目最后一次更新）：6.12.12-2-MANJARO stable
- 编译内核时的GCC版本：9.3.0
- QEMU版本：9.2.0

## 修改基于
- EDK2： edk2-stable202408.01（commit id = `4dfdca63a93497203f197ec98ba20e2327e4afe4`）
- Ubuntu：ubuntu-20.04-server-cloudimg-amd64.img (sha256 = `8d73e811f51e1a82785f3fa26f10bdcc150fddf8d8c79d7417e645535d745ac0`)
- Linux Kernel：linux-5.4.290（Ubuntu 20.04 LTS GA longterm内核）

## 文件结构
```
- resources # 资源文件
  - static # 资源文件的原始版本
  - original # 提供Linux内核和edk2原始版本解压后的状态，用于生成patch
- linux-5.4.290 # 编辑中的Linux内核代码，仅包含代码，配置不应该出现在这里
- edk2 # 编辑中的edk2代码，仅包含代码，配置不应该出现在这里
- tools
  - config # 除了`.gitignore`等必须出现在根目录的配置文件，其他所有配置文件都应该在这里
  - scripts # 所有脚本都应该在这里
- testcases # 一些自动化测试的资源文件（例如，具体数据，以及和具体测试点相关的脚本。但是拉起测试的脚本应该在之前的`tools/scripts`目录里）
- build # 编译进行的目录，里面可能有多个子目录用于分别编译不同的东西
- playground # 测试进行的目录
```