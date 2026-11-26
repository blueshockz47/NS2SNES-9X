#pragma once
namespace graphics_policy {
inline bool validUpload(int w,int h,int pitch,int maxW,int maxH){return w>0&&h>0&&w<=maxW&&h<=maxH&&pitch>=w;}
inline int shaderChoice(int requested,int count,bool available){return requested>=0&&requested<count&&available?requested:0;}
inline bool signalReady(int requested,bool program,bool framebuffer){return requested>0&&program&&framebuffer;}
}
