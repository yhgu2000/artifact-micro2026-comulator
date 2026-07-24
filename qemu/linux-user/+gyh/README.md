# 构建方法

```bash
mkdir build && cd build
../configure \
  --disable-system \
  --target-list=x86_64-linux-user,aarch64-linux-user,riscv64-linux-user \
  --enable-debug \
  --enable-debug-tcg \
  --cc=/bin/clang \
  --cxx=/bin/clang++ \
  --extra-ldflags=-rdynamic \
  --disable-werror
bear -- ninja
sed -i 's/"-iquote",/"-I",/g' compile_commands.json
# VSCode 不识别 -iquote 选项, 因此替换为 -I
```
