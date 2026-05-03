# Third-Party Notices

MyPlayer uses or can link against the following third-party components. This repository does not vendor the large binary dependency bundle, runtime DLLs, model files or test media. Users are responsible for obtaining dependencies from their original sources and complying with their licenses.

## Source Code Included In This Repository

### ByteTrack

- Location: `src/ByteTrack`
- Upstream: https://github.com/FoundationVision/ByteTrack
- License: MIT License
- Copyright: Copyright (c) 2021 Yifu Zhang
- Notice: the ByteTrack license text is preserved in `src/ByteTrack/LICENSE`.

## External Dependencies Not Vendored In This Repository

### Qt

- Website: https://www.qt.io/
- License: available under commercial and open-source license options.
- Notice: if distributing Qt runtime libraries or plugins, comply with the selected Qt license terms.

### FFmpeg

- Website: https://ffmpeg.org/
- License: FFmpeg is commonly LGPL, but specific builds can become GPL or include nonfree components depending on configuration.
- Notice: if distributing FFmpeg binaries, verify the exact build configuration and include the required license notices.

### CUDA Toolkit and NVIDIA Runtime Libraries

- Website: https://developer.nvidia.com/cuda-toolkit
- License: NVIDIA software license terms.
- Notice: CUDA and related NVIDIA runtime files are not part of this repository.

### ONNX Runtime

- Upstream: https://github.com/microsoft/onnxruntime
- License: MIT License.
- Notice: ONNX Runtime binaries are not part of this repository.

### OpenCV

- Website: https://opencv.org/
- License: Apache License 2.0.
- Notice: OpenCV binaries are not part of this repository.

### Eigen

- Website: https://eigen.tuxfamily.org/
- License: Mozilla Public License 2.0 for Eigen core, with some components under other compatible open-source licenses.
- Notice: Eigen headers are not part of this repository.

### libass

- Upstream: https://github.com/libass/libass
- License: ISC License.
- Notice: libass is resolved from the local dependency bundle.

### whisper.cpp / Whisper Runtime

- Upstream: https://github.com/ggerganov/whisper.cpp
- License: MIT License.
- Notice: Whisper libraries and model files are not part of this repository.

## Models, Labels And Test Media

Model files, labels and test videos are intentionally excluded from this repository. Before using or redistributing any model or dataset-derived file, check the license from the original source.

## No Warranty

Third-party components are provided by their upstream authors under their own licenses. MyPlayer is provided under the MIT License without warranty.
