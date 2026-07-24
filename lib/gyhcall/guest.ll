; 调试输出函数，参数同 printf
declare void @__gyh_debug__(
  ptr noundef noalias nocapture readonly %fmt,
  ...
)

; ============================================================================ ;

; ; 库 UID 变量示例, 链接时生成
; ; 其中 hhh 会被替换为 32 位随机值
; @__gyh_qlib_uid_hhh = dso_local global i32 u0x12345678, align 8

; 向主侧注册库, 汇编实现
; 参数固定传入指针地址, 主侧将分配的索引置入 %qlibUid
declare void @__gyh_ctor__(
  ptr noundef noalias nocapture %qlibUid  ; @__gyh_qlib_uid_hhh
)

; ; 加到 @llvm.global_ctors 且首个调用, 链接时生成
; ; 其中 hhh 会被替换为库 UID
; define void @__gyh_ctor_hhh() { ... }

; 向主侧注销库, 汇编实现
; 参数固定传入指针地址, 主侧重置 %qlibUid
declare void @__gyh_dtor__(
  ptr noundef noalias nocapture %qlibUid  ; @__gyh_qlib_uid_hhh
)

; ; 加到 @llvm.global_dtors, 最后调用, 链接时生成
; ; 其中 hhh 会被替换为库 UID
; define void @__gyh_dtor_hhh() { ... }

; 客方到主侧的系统调用，汇编实现，客方架构相关
declare void @__gyh_call__(
  ptr noundef noalias nocapture readonly %qlibUid,  ; @__gyh_qlib_uid_hhh
  i32 %funcUid,
  ptr noundef noalias nocapture writeonly %retPad,
  ptr noundef noalias nocapture readonly %argPad,
  ptr noundef noalias nocapture readonly %extRefs,
  ptr noundef noalias nocapture readonly %cbStubs
)

; 主侧到客方的回调入口，汇编实现，客方架构相关
declare void @__gyh_callback_return__(
  ptr noundef noalias nocapture writeonly %retPad,
  ptr noundef noalias nocapture readonly %argPad,
  ptr noundef noalias nocapture readonly %gfunc,
  ptr noundef noalias nocapture readonly %cbStub
) ; 参数顺序与回调桩匹配, 可以避免汇编实现时移动寄存器

; ; 客方回调承接桩签名示例，链接时生成
; ; 其中 hhh 会被替换为函数类型哈希
; define void @__gyh_callback_hhh(
;   ; 返回值指针
;   ptr noundef noalias nocapture writeonly %retPad,
;   ; 参数结构体指针
;   ptr noundef noalias nocapture readonly %argPad,
;   ; 客方函数地址
;   ptr noundef noalias nocapture readonly %gfunc
; ) { ... }

; ============================================================================ ;
; 启用 PFO 优化
; ============================================================================ ;

; ; 外联函数示例, 链接时生成
; ; Function Attrs: noinline optnone
; define weak i32 @__gyh_outliner_hhh(...) { ... }

; ============================================================================ ;
; 启用 GRT 优化
; ============================================================================ ;

; ; 全局引用表, 链接时生成
; ; 其中 hhh 会被替换为库 UID
; @__gyh_extreftbl_hhh = internal constant [N x ptr] [ptr @xxx]

; ; 全局回调桩表, 链接时生成
; ; 其中 hhh 会被替换为库 UID
; @__gyh_cbstubtbl_hhh = internal constant [N x ptr] [ptr @xxx]

; 参数固定传入指针地址, 主侧将分配的索引置入 %qlibUid
; 加到 @llvm.global_ctors, 首个调用
declare void @__gyh_ctor_grt__(
  ptr noundef noalias nocapture %qlibUid,             ; @__gyh_qlib_uid_xxx
  ptr noundef noalias nocapture readonly %extreftbl,  ; @__gyh_extreftbl__
  ptr noundef noalias nocapture readonly %cbstubtbl   ; @__gyh_cbstubtbl__
)

; 此时 extreftbl 和 cbstubtbl 都已经在加载时注册, 因此无需再传入
declare void @__gyh_call_grt__(
  ptr noundef noalias nocapture readonly %qlibUid,
  i32 %funcUid,
  ptr noundef noalias nocapture writeonly %retPad,
  ptr noundef noalias nocapture readonly %argPad
)

; ============================================================================ ;
; 启用 FCP-CM 优化
; ============================================================================ ;

; ; 客方被卸载的函数表, 链接时生成
; @__gyh_gfunctbl__ = internal constant [N x ptr] [ptr @__gyh_call_0, ...]

; 在 GRT 的基础上, 遍历 __gyh_gfunctbl__ 并设置函数的前缀数据
declare void @__gyh_ctor_fcp_cm__(
  ptr noundef noalias nocapture %qlibUid,             ; @__gyh_qlib_uid_xxx
  ptr noundef noalias nocapture readonly %extreftbl,  ; @__gyh_extreftbl__
  ptr noundef noalias nocapture readonly %cbstubtbl,  ; @__gyh_cbstubtbl__
  ptr noundef noalias nocapture %gfunctbl,  ; @__gyh_gfunctbl__
  i32 %gfunctblSize
)
