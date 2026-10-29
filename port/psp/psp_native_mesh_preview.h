#ifndef TP3_PSP_NATIVE_MESH_PREVIEW_H
#define TP3_PSP_NATIVE_MESH_PREVIEW_H
/* Optional, explicitly enabled isolated PSP GU renderer for M3P1 and A3P1.
 * Not a replacement for the original BT3 stage/fighter renderer: no
 * original skeleton binding, material map or skin weights are known.
 * The separate preview is a real executable proof of native GU use. */
void PspNativePreview_Init(void);
void PspNativePreview_Draw(unsigned frame);
#endif
