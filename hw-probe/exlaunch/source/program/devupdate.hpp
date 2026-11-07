/* Dev updates over Wi-Fi from tools/devserver.py (opt-in: sd:/xplay/devserver.txt). devupdate.cpp. */
#pragma once

namespace devupdate {
void Start();                                          /* at boot, after the SD is mounted: reads devserver.txt, starts the poll thread */
bool Enabled();
void Frame();                                          /* each frame on the game thread: the notice on the main menu */
void ShowNotice(void* scene, const char* text, float y);   /* a line on the main menu at y (232 the bottom; also TF.NX's updater's) */
bool Upload(const char* name, const char* data, int len);   /* POST /upload/<name> */
void RequestUpload(const char* name, const char* sdPath);   /* UploadFile on the dev-update thread, soon */
bool UploadFile(const char* name, const char* sdPath);
}
