#ifndef DEJAVIS_APP_AV_ENCDEC_GLOBALS_H
#define DEJAVIS_APP_AV_ENCDEC_GLOBALS_H


struct YUVFrameData {
    void* mappedPtr;
    VkSubresourceLayout layouts[3];
    double pts = 0.0;
};

#endif //DEJAVIS_APP_AV_ENCDEC_GLOBALS_H