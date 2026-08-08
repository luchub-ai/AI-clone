#include "utils/encode_base64.h"
#include <iostream>
using namespace std;


void testSendToVLM() {
    std::string img_path = "workspace/screenshot.png";
    auto base64_data = ImageUtils::imageToBase64String(img_path);

    cout << base64_data.value() << endl;
}

int main()
{
    testSendToVLM();
    return 0;
}