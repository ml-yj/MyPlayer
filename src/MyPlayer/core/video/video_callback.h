
#pragma once

struct AVFrame;

class VideoCallback
{
public:

    virtual ~VideoCallback() = default;

    virtual void Init(int width, int height) = 0;

    virtual void Repaint(AVFrame* frame) = 0;

    virtual void SetCudaContext(void* ctx) {}

    virtual void SetClosing(bool closing) {}
};
