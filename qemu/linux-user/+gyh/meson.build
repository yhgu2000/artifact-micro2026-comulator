linux_user_ss.add(files(
  'gyh.c',
  'c.cpp',
))

linux_user_ss.add(when: 'TARGET_X86_64', if_true: [
  files('x86_64-on.c'),
])

linux_user_ss.add(when: 'TARGET_AARCH64', if_true: [
  files('aarch64-on.c'),
])

linux_user_ss.add(when: 'TARGET_RISCV64', if_true: [
  files('riscv64-on.c'),
])
