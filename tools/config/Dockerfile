# 使用gcc 9.3.0作为基础镜像
FROM gcc:9.3.0

# 更新包列表并安装必要的依赖
RUN apt-get update && \
    apt-get install -y \
    flex \
    bison \
    bc \
    libelf-dev \
    && rm -rf /var/lib/apt/lists/*

# 设置工作目录
WORKDIR /workspace

# 添加说明标签
LABEL maintainer="StrangeLinux Team" \
      description="Linux kernel 5.4.290 compilation environment" 