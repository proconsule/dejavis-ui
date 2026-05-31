#include <AVFoundation/AVFoundation.h>

void requestMicrophonePermission() {
    [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio
                             completionHandler:^(BOOL granted) {
        if (granted) {
            NSLog(@"Microfono: accesso concesso");
        } else {
            NSLog(@"Microfono: accesso negato");
        }
    }];
}