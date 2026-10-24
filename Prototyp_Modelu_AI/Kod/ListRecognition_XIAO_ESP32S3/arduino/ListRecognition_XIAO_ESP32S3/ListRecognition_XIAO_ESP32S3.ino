
#include <Arduino.h>
#include <ListRecognition_inferencing.h>

static const size_t IMAGE_WIDTH = EI_CLASSIFIER_INPUT_WIDTH;
static const size_t IMAGE_HEIGHT = EI_CLASSIFIER_INPUT_HEIGHT;
static const size_t IMAGE_SIZE = IMAGE_WIDTH * IMAGE_HEIGHT * 3;

static uint8_t *imageData = nullptr;

static bool readExactBytes(uint8_t *destination, size_t length) {
    size_t received = 0;
    uint32_t lastByteTime = millis();

    while (received < length) {
        while (Serial.available() > 0 && received < length) {
            int value = Serial.read();

            if (value >= 0) {
                destination[received++] = (uint8_t)value;
                lastByteTime = millis();
            }
        }

        if (millis() - lastByteTime > 60000) {
            Serial.printf(
                "ERROR,IMAGE_RECEIVE_TIMEOUT,%u,%u\n",
                (unsigned int)received,
                (unsigned int)length
            );
            return false;
        }

        delay(1);
    }

    return true;
}

static int getImageData(size_t offset, size_t length, float *out) {
    const size_t pixelCount = IMAGE_WIDTH * IMAGE_HEIGHT;

    if (offset + length > pixelCount) {
        return -1;
    }

    for (size_t i = 0; i < length; i++) {
        size_t index = (offset + i) * 3;

        uint32_t r = imageData[index];
        uint32_t g = imageData[index + 1];
        uint32_t b = imageData[index + 2];

        out[i] = (float)((r << 16) | (g << 8) | b);
    }

    return 0;
}

static void classifyImage() {
    signal_t signal;
    signal.total_length = IMAGE_WIDTH * IMAGE_HEIGHT;
    signal.get_data = getImageData;

    ei_impulse_result_t result = {};

    Serial.println("CLASSIFICATION_STARTED");

    EI_IMPULSE_ERROR status = run_classifier(&signal, &result, false);

    if (status != EI_IMPULSE_OK) {
        Serial.printf("ERROR,CLASSIFIER,%d\n", (int)status);
        Serial.println("FINISHED");
        return;
    }

    for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
        Serial.printf(
            "RESULT,%s,%.6f\n",
            result.classification[i].label,
            result.classification[i].value
        );
    }

    Serial.println("FINISHED");
}

void setup() {
    Serial.begin(115200);

    uint32_t start = millis();
    while (!Serial && millis() - start < 5000) {
        delay(10);
    }

    if (psramFound()) {
        imageData = (uint8_t *)ps_malloc(IMAGE_SIZE);
    }

    if (imageData == nullptr) {
        imageData = (uint8_t *)malloc(IMAGE_SIZE);
    }

    if (imageData == nullptr) {
        Serial.println("ERROR,IMAGE_MEMORY_ALLOCATION");
        return;
    }

    Serial.printf(
        "IDLE,%u,%u,%u\n",
        (unsigned int)IMAGE_WIDTH,
        (unsigned int)IMAGE_HEIGHT,
        (unsigned int)IMAGE_SIZE
    );
}

void loop() {
    if (imageData == nullptr || Serial.available() <= 0) {
        delay(1);
        return;
    }

    String header = Serial.readStringUntil('\n');
    header.trim();

    if (!header.startsWith("IMG ")) {
        return;
    }

    size_t requestedSize = (size_t)header.substring(4).toInt();

    if (requestedSize != IMAGE_SIZE) {
        Serial.printf(
            "ERROR,IMAGE_SIZE,%u,%u\n",
            (unsigned int)requestedSize,
            (unsigned int)IMAGE_SIZE
        );
        return;
    }

    Serial.printf(
        "READY,%u,%u\n",
        (unsigned int)IMAGE_WIDTH,
        (unsigned int)IMAGE_HEIGHT
    );

    if (!readExactBytes(imageData, IMAGE_SIZE)) {
        return;
    }

    Serial.printf("IMAGE_RECEIVED,%u\n", (unsigned int)IMAGE_SIZE);

    classifyImage();
}
