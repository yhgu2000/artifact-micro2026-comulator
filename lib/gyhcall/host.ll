; 调试输出函数，参数同 printf
declare void @__gyh_debug__(
  ptr noundef noalias nocapture readonly %fmt,
  ...
)

; ============================================================================ ;

; ; 主侧承接函数表及其大小, 链接时生成
; ; 其中 hhh 会被替换为库 UID
; @__gyh_hfunctbl_hhh = constant [N x ptr] [ptr @__gyh_call_0, ...]
; @__gyh_hfunctbl_size_hhh = constant i32 N

; ; 主侧卸载函数示例，其中 ddd 会被替换为函数 UID
; define internal void @__gyh_call_ddd(
;   ; 返回值指针
;   ptr noundef noalias nocapture writeonly %retPad,
;   ; 参数结构体指针
;   ptr noundef noalias nocapture readonly %argPad,
;   ; 外部引用指针
;   ptr noundef noalias nocapture readonly %extRefs,
;   ; 回调桩指针
;   ptr noundef noalias nocapture readonly %cbStubs
; )

; 主侧到客方的回调处理, QEMU 中实现
declare void @__gyh_callback__(
  ; 客方回调桩
  ptr noundef noalias nocapture readonly %cbStub,
  ; 客方函数地址
  ptr noundef noalias nocapture readonly %gfunc,
  ; 返回值指针
  ptr noundef noalias nocapture writeonly %retPad,
  ; 参数结构体指针
  ptr noundef noalias nocapture readonly %argPad
)

; ============================================================================ ;
; 启用 GRT 优化
; ============================================================================ ;

; ; 客方全局引用表和全局回调桩表指针, 链接时生成
; ; 其中 xxx 会被替换为库 UID, 运行时 QEMU 将它们置为客方传入的地址
; @__gyh_extreftbl_xxx = dso_local global ptr null
; @__gyh_cbstubtbl_xxx = dso_local global ptr null

; ; 此时卸载函数直接通过上面两个变量访问外部引用和回调桩, 因此无需再传入
; define internal void @__gyh_call_ddd(
;   ; 返回值指针
;   ptr noundef noalias nocapture writeonly %retPad,
;   ; 参数结构体指针
;   ptr noundef noalias nocapture readonly %argPad
; )

; ============================================================================ ;
; 启用 FCP-CM 优化
; ============================================================================ ;

; ; 卸载的原函数表, 链接时生成
; ; 主侧在加载 Qlib 时会遍历这张表, 将函数地址写到客方函数前缀中
; @__gyh_functbl_hhh = constant [N x ptr] [ptr @xxx, ...]
