// SPDX-License-Identifier: GPL-3.0-or-later
void captureTestWindow(HWND h,const wchar_t* name) {
    RECT rect{};GetWindowRect(h,&rect);int width=rect.right-rect.left,height=rect.bottom-rect.top;
    HDC window=GetDC(h),memory=CreateCompatibleDC(window);HBITMAP bitmap=CreateCompatibleBitmap(window,width,height);
    auto previous=SelectObject(memory,bitmap);UpdateWindow(h);PrintWindow(h,memory,0);SelectObject(memory,previous);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    std::vector<char> pixels((size_t)width*height*4);GetDIBits(memory,bitmap,0,height,pixels.data(),&info,DIB_RGB_COLORS);
    BITMAPFILEHEADER header{};header.bfType=0x4D42;header.bfOffBits=sizeof(header)+sizeof(info.bmiHeader);header.bfSize=header.bfOffBits+(DWORD)pixels.size();
    wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);std::ofstream file(std::filesystem::path(exe).parent_path()/name,std::ios::binary);
    file.write((const char*)&header,sizeof(header));file.write((const char*)&info.bmiHeader,sizeof(info.bmiHeader));file.write(pixels.data(),pixels.size());
    DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(h,window);
}
