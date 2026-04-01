#include <Arduino.h>
#include "driver/rtc_io.h"
#include "audio.h"


#define BUTTON_PIN_BITMASK(GPIO) (1ULL << GPIO)
#define WAKEUP_PIN_MASK (1ULL << GPIO_NUM_4)
#define USE_EXT0_WAKEUP 1
#define WAKEUP_GPIO GPIO_NUM_4

#define uS_TO_S_FACTOR 1000000ULL
#define TIME_TO_SLEEP 5

RTC_DATA_ATTR int bootCount = 0;
struct Button {
    const uint8_t PIN;
    bool pressed;
    unsigned long lastMillis;
};

Button leftFoot = {4, false, 0};
Button rightFoot = {0, false, 0};
Button leftHand = {1, false, 0};
Button rightHand = {2, false, 0};
int motorPin = 5;
const unsigned long DEBOUNCE_TIME = 250;

void IRAM_ATTR isr(void* arg) {
    
    Button* b = static_cast<Button*>(arg);
    unsigned long now = millis();
    if (now - b->lastMillis > DEBOUNCE_TIME) {
        b->pressed = true;
        b->lastMillis = now;
        //setAudioAbort(true);
    }
}
int audioOptionIndex = 0;
bool storytelling = false;
bool messagesOption = false;
VolumeLevel volume = VolumeLevel::QUIET;
int storyIndex = 0;
int messageIndex = 0;
String audioActions[] = {"Storytelling", "WhiteNoise", "Messages", "Meditation/Breathing"};
struct StoryNode{
    String text;
    int optionA;
    int optionB;
    const char* audioFile;
};
StoryNode storyTree[] = {
    {"Start: You are walking in a forest! Suddenly, you come across a fork in the road, go [Left] or [Right]?", 1, 2, "/test.wav"}, //0
    {"Went Left!: You come across a cave, [Go Inside] or [Keep Walking]?", 3, 4, "/test.wav"}, //1
    {"Went Right!: You reach a cliff, [Cross the Bridge] or [Climb Down]?", 5, 6, "/test.wav"}, //2
    {"Went into the cave!: You see a dragon! [Fight] or [Run Away]?", 7, 8, "/test.wav"}, //3
    {"Kept Walking: You come across a bear! Oh No! Run the the [Left] or [Right]?", 9, 10, "/test.wav"}, //4
    {"Crossed the Bridge: There is a cottage and a palace, go inside the [Cottage] or the [Palace]?", 11, 12, "/test.wav"}, //5
    {"Climb Down: There is a river ahead! [Bravely Swim] or [Sail a Boat]?", 13, 14, "/test.wav"}, //6
    {"Ending 7: You fought the dragon and gained treasure! Congrats!", 0, 0, "/test.wav"}, //7
    {"Ending 8: You ran away and got home safe and sound!", 0, 0, "/test.wav"}, // 8
    {"Ending 9: You ran to the left, but the bear caught you! :( Better luck next time!", 0, 0, "/test.wav"}, //9
    {"Ending 10: You ran to the right and managed to escape by hiding in a tunnel, Yay!", 0, 0, "/test.wav"}, //10
    {"Ending 11: You went into the cottage and found a witch brewing a soup! You are now in the soup :(", 0, 0, "/test.wav"},
    {"Ending 12: You went into the palace and and were elected to be the new ruler of the kingdom, best of luck!", 0, 0, "/test.wav"},
    {"Ending 13: You tried to swim across the river got chased by alligators, but you fought bravely and made it across!", 0, 0, "/test.wav"},
    {"Ending 14: You took a boat and tried to cross, but the boat sank and you had to turn back, and climb back up the cliff", 0, 0, "/test.wav"}
};
String messages[] = {"Hi", "Hello", "Welcome", "Greeting"};

int messageCount = sizeof(messages) / sizeof(messages[0]);
int actionsCount = sizeof(audioActions) / sizeof(audioActions[0]);

void print_wakeup_reason() {
    esp_sleep_wakeup_cause_t wakeup_reason;
    wakeup_reason = esp_sleep_get_wakeup_cause();
    switch(wakeup_reason)
    {
        case ESP_SLEEP_WAKEUP_EXT0 : Serial.println("Wakeup caused by external signal using RTC_IO"); break;
        case ESP_SLEEP_WAKEUP_EXT1 : Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
        case ESP_SLEEP_WAKEUP_TIMER : Serial.println("Wakeup caused by timer"); break;
        case ESP_SLEEP_WAKEUP_TOUCHPAD : Serial.println("Wakeup caused by touchpad"); break;
        case ESP_SLEEP_WAKEUP_ULP : Serial.println("Wakeup caused by ULP program"); break;
        default : Serial.printf("Wakeup was not caused by deep sleep: %d\n",wakeup_reason); break;
    }
}


SPIClass sdSPI(FSPI);

// List all dirs and files stored in microSD card, including dir name, filename, and size (bytes).
void listDir(fs::FS &fs, const char * dirname, uint8_t levels) {
  File root = fs.open(dirname);
  if (!root) {
    Serial.println("Failed to open directory");
    return;
  }

  File file = root.openNextFile();

  while (file) {
    if (file.isDirectory()) {
      Serial.print("DIR : ");
      Serial.println(file.name());
    } else {
      Serial.print("FILE: ");
      Serial.print(file.name());
      Serial.print("  SIZE: ");
      Serial.println(file.size());
    }
    file = root.openNextFile();
  }
}

void setup() {
    Serial.begin(115200);
    Serial.println("starting");
    while(!Serial);
    delay(1000);
    ++bootCount;
    Serial.println("Boot number: " + String(bootCount));    
    // print_wakeup_reason();
    // esp_deep_sleep_enable_gpio_wakeup(WAKEUP_PIN_MASK, ESP_GPIO_WAKEUP_GPIO_LOW);
    // rtc_gpio_init(GPIO_NUM_4);
    // rtc_gpio_set_direction(GPIO_NUM_4, RTC_GPIO_MODE_INPUT_ONLY);
    // rtc_gpio_pullup_en(GPIO_NUM_4);
    // rtc_gpio_pulldown_dis(GPIO_NUM_4);
    initSinTable();
    setupI2S(SAMPLE_RATE, 1);
    sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    if (!SD.begin(SD_CS, sdSPI)) {
        Serial.println("Card Mount Failed");
        return;
    }

    Serial.println("SD card initialized.");
    Serial.println("Files on card:");

    listDir(SD, "/", 0);
    pinMode(motorPin, OUTPUT);
    pinMode(leftFoot.PIN, INPUT_PULLUP);
    pinMode(rightFoot.PIN, INPUT_PULLUP);
    pinMode(leftHand.PIN, INPUT_PULLUP);
    pinMode(rightHand.PIN, INPUT_PULLUP);
    attachInterruptArg(leftFoot.PIN, isr, &leftFoot, FALLING);
    attachInterruptArg(rightFoot.PIN, isr, &rightFoot, FALLING);
    attachInterruptArg(leftHand.PIN, isr, &leftHand, FALLING);
    attachInterruptArg(rightHand.PIN, isr, &rightHand, FALLING);

    // Assign audio task to core 1
    xTaskCreatePinnedToCore(audioTask, "audioTask", 4096, NULL, 1, NULL, 1);
}
void loop() {
    if (leftHand.pressed) {
        leftHand.pressed = false;

        if (storytelling) {
            storyIndex = storyTree[storyIndex].optionA;
            Serial.println(storyTree[storyIndex].text);
            playWav(storyTree[storyIndex].audioFile);
            Serial.println("Playback finished.");
        } else if (messagesOption){
            messageIndex = (messageIndex + 1) % messageCount;
            Serial.println(messages[messageIndex]);
            playWav("/test.wav");
            Serial.println("Playback finished.");
        }
    }
    if (rightHand.pressed) {
        rightHand.pressed = false;
        if (storytelling){
            storyIndex = storyTree[storyIndex].optionB;
            Serial.println(storyTree[storyIndex].text);
            playWav(storyTree[storyIndex].audioFile);
            Serial.println("Playback finished.");
        } else if (messagesOption){
            messageIndex--;
            if (messageIndex < 0){
                messageIndex = messageCount - 1;
            }
            Serial.println(messages[messageIndex]);
            playWav("/test.wav");
            Serial.println("Playback finished.");
        }
    }
    if (leftFoot.pressed) {
        leftFoot.pressed = false;
        Serial.print("Left Foot.    ");
        setVolume(++volume);
        
        Serial.printf("Volume Level: %d\n", static_cast<int>(volume));
        // Play a short beep to confirm volume change (if not playing other audio)
        if (!isToneActive() && !isWavActive()) playTone(440.0, 100);
    }
    if (rightFoot.pressed) {
        rightFoot.pressed = false;
        audioOptionIndex = (audioOptionIndex + 1) % actionsCount;
        Serial.print("Right Foot.   ");
        Serial.print("Now playing: ");
        Serial.println(audioActions[audioOptionIndex]);
        storytelling = (audioOptionIndex == 0);
        if (storytelling) {
            Serial.println(storyTree[0].text);
            storyIndex = 0;
            playWav(storyTree[storyIndex].audioFile);
            Serial.println("Playback finished.");
        } else if (audioOptionIndex == 1){
            Serial.println("play white noise audio");
        } else if (audioOptionIndex == 3){
            Serial.println("play meditation audio");
        }
        messagesOption = (audioOptionIndex == 2);
    }
}