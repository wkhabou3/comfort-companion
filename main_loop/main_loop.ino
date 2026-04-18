#include <Arduino.h>
#include <cstring>
#include <string>
#include <unordered_map>
#include "driver/rtc_io.h"
#include "audio.h"


#define BUTTON_PIN_BITMASK(GPIO) (1ULL << GPIO)
#define WAKEUP_PIN_MASK (1ULL << GPIO_NUM_4)
#define USE_EXT0_WAKEUP 1
#define WAKEUP_GPIO GPIO_NUM_4
#define BUZZ_DURATION 100

#define uS_TO_S_FACTOR 1000000ULL
#define TIME_TO_SLEEP 5

RTC_DATA_ATTR int bootCount = 0;
struct Button {
    const uint8_t PIN;
    volatile bool pressed;
    volatile unsigned long lastMillis;
};
enum BreathState {
    IDLE,
    INHALE,
    HOLD1,
    EXHALE,
    HOLD2
};

enum HeartbeatState {
    HB_IDLE,
    HB_BEAT1_ON,
    HB_BEAT1_OFF,
    HB_BEAT2_ON,
    HB_BEAT2_OFF,
};

HeartbeatState heartbeatState = HB_IDLE;
unsigned long heartbeatTimer = 0;

BreathState breathState = IDLE;
unsigned long breathTimer = 0;
Button leftFoot = {11, false, 0};
Button rightFoot = {12, false, 0};
Button leftHand = {47, false, 0};
Button rightHand = {48, false, 0};
int motor1Pin = 9;
int motor2Pin = 10;
const unsigned long DEBOUNCE_TIME = 300;

std::unordered_map<std::string, float> fileGains;

void IRAM_ATTR isr(void* arg) {
    
    Button* b = static_cast<Button*>(arg);
    unsigned long now = millis();
    if (now - b->lastMillis > DEBOUNCE_TIME) {
        b->pressed = true;
        b->lastMillis = now;
        //setAudioAbort(true);
    }
}

void setHaptics(bool on) {
    digitalWrite(motor1Pin, on);
    digitalWrite(motor2Pin, on);
}

void buzzMotor() {
    setHaptics(true);
    delay(BUZZ_DURATION);
    setHaptics(false);
}

void breathingExercise() {
    Serial.println("Breathe in");
    playWav("/test.wav", fileGains["/test.wav"]); //"BREATHE IN"

    setHaptics(true);
    delay(4000);
    setHaptics(false);

    delay(4000);

    Serial.println("Breathe out");
    playWav("/test.wav", fileGains["/test.wav"]); //"BREATHE OUT"

    setHaptics(true);
    delay(4000);
    setHaptics(false);
    delay(4000);
}

void breathingExerciseNoBlock() {
    unsigned long now = millis();

    switch(breathState) {
        case IDLE:
            setHaptics(true);
            Serial.println("Breathe In");
            playWav("/test.wav", fileGains["/test.wav"]);
            setHaptics(true);
            breathTimer = now;
            breathState = INHALE;
            break;
        case INHALE:
            if (now - breathTimer >= 4000) {
                setHaptics(false);
                Serial.println("Hold");
                playWav("/test.wav", fileGains["/test.wav"]);
                // setHaptics(false);
                breathTimer = now;
                breathState = HOLD1;
            }
            break;
        case HOLD1:
            if (now - breathTimer >= 4000) {
                setHaptics(true);
                Serial.println("Breath out");
                playWav("/test.wav", fileGains["/test.wav"]);
                setHaptics(true);
                breathTimer = now;
                breathState = EXHALE;
            }
            break;
        case EXHALE:
            if (now - breathTimer >= 4000) {
                setHaptics(false);
                Serial.println("Hold");
                playWav("/test.wav", fileGains["/test.wav"]);
                breathTimer = now;
                breathState = HOLD2;
            }
            break;
        case HOLD2:
            if (now - breathTimer >= 4000) {
                breathState = IDLE;
            }
            break;
    }
}
void heartbeatOption(int beats){
    for (int i = 0; i < beats; i++) {
        setHaptics(true);
        delay(120);
        setHaptics(false);
        delay(80);
        setHaptics(true);
        delay(80);
        setHaptics(false);
        delay(600);
    }
}

void heartbeatNonBlocking(){
    unsigned long now = millis();
    switch (heartbeatState) {
        case HB_IDLE:
            setHaptics(true);
            heartbeatTimer = now;
            heartbeatState = HB_BEAT1_ON;
            break;
        case HB_BEAT1_ON:
            if (now - heartbeatTimer >= 120) {
                setHaptics(false);
                heartbeatTimer = now;
                heartbeatState = HB_BEAT1_OFF;
            }
            break;
        case HB_BEAT1_OFF:
            if (now - heartbeatTimer >= 80) {
                setHaptics(true);
                heartbeatTimer = now;
                heartbeatState = HB_BEAT2_ON;
            }
            break;
        case HB_BEAT2_ON:
            if (now - heartbeatTimer >= 80) {
                setHaptics(false);
                heartbeatTimer = now;
                heartbeatState = HB_BEAT2_OFF;
            }
            break;
        case HB_BEAT2_OFF:
            if (now - heartbeatTimer >= 600) {
                setHaptics(true);
                heartbeatTimer = now;
                heartbeatState = HB_BEAT1_ON;
            }
            break;
    }
}

int audioOptionIndex = 0;
bool storytelling = false;
bool medicalStoryTime = false;
bool breathingMode = false;
bool heartbeatMode = false;
bool messagesOption = false;
VolumeLevel volume = VolumeLevel::QUIET;
int storyIndex = 0;
int medicalIndex = 0;
int messageIndex = 0;
String audioActions[] = {
    "Storytelling", 
    "WhiteNoise", 
    "Messages", 
    "Meditation/Breathing", 
    "MedicalStory",
    "Heartbeat"
};
struct StoryNode{
    String text;
    int optionA;
    int optionB;
    const char* audioFile;
};
StoryNode storyTree[] = { 
    {"Start: You are walking in a forest! Suddenly, you come across a fork in the road, press my left paw to go left or my right paw to go right.", 1, 2, "/story0.wav"}, //0 
    {"Went Left!: You come across a cave, press my left paw to go inside or my right paw to keep walking?", 3, 4, "/story1.wav"}, //1 
    {"Went Right!: You reach a cliff, [press my left paw to cross the bridge] or [press my right paw to climb down]?", 5, 6, "/story2.wav"}, //2 
    {"Went into the cave!: You see a dragon! [Fight] or [Run Away]?", 7, 8, "/story3.wav"}, //3 
    {"Kept Walking: You come across a bear! Oh No! Run to the [Left] or [Right]?", 9, 10, "/story4.wav"}, //4 
    {"Crossed the Bridge: There is a cottage and a palace, go inside the [Cottage] or the [Palace]?", 11, 12, "/story5.wav"}, //5 
    {"Climb Down: There is a river ahead! [Bravely Swim] or [Sail a Boat]?", 13, 14, "/story6.wav"}, //6 
    {"Ending 7: You fought the dragon and gained treasure! Congrats!", 0, 0, "/story7.wav"}, //7 
    {"Ending 8: You ran away and got home safe and sound!", 0, 0, "/story8.wav"}, // 8 
    {"Ending 9: You ran to the left, but the bear caught you! :( Better luck next time!", 0, 0, "/story9.wav"}, //9 
    {"Ending 10: You ran to the right and managed to escape by hiding in a tunnel, Yay!", 0, 0, "/story10.wav"}, //10 
    {"Ending 11: You went into the cottage and found a witch brewing a soup! You are now in the soup :(", 0, 0, "/story11.wav"}, //11
    {"Ending 12: You went into the palace and and were elected to be the new ruler of the kingdom, best of luck!", 0, 0, "/story12.wav"}, //12
    {"Ending 13: You tried to swim across the river got chased by alligators, but you fought bravely and made it across!", 0, 0, "/story13.wav"}, //13
    {"Ending 14: You took a boat and tried to cross, but the boat sank and you had to turn back, and climb back up the cliff", 0, 0, "/story14.wav"} //14
}; 

StoryNode medicalStory[] = {
    {"Lets walk through a needle biopsy together, [press one of my paws to continue]" , 1, 1, "/medical0.wav"}, //0 
    {"Step One, Preparation [Press my left paw if you want to hear more] or [my right paw for the next step ]?", 2, 3, "/medical1.wav"}, //1 
    {"A doctor will clean your skin to kill all the germs and rub a special cream to stop you from feeling pain in that area, [press my left paw to hear this again] or [press my right paw for the Next Step]?", 2, 3, "/medical2.wav"}, //2 
    {"Step two, Sample Collection [Press my left paw if you want to hear more] or [my right paw for the next step ]?", 4, 5, "/medical3.wav"}, //3 
    {"The doctor will use a small needle to take a small sample to learn more about you and how to help, [press my left paw to hear this again] or [press my right paw for the Next Step]?", 4, 5, "/medical4.wav"}, //4 
    {"Step Three, A Doctor might use a camera to help! [Press my left paw if you want to hear more] or [my right paw for the next step ]?", 6, 7, "/medical5.wav"}, //5 
    {"If the bump in your skin is deep pictures will be used to guide the needle, [press my left paw to hear this again] or [press my right paw for the Next Step]?", 6, 7, "/medical6.wav"}, //6 
    {"Step Four: Sealing up!. [Press my left paw if you want to hear more] or [my right paw for the next step ]?", 8, 9, "/medical7.wav"}, //7 
    {"The doctors might stitch the skin back together, then they will apply a band-aid to help you feel better, [[press my left paw to hear this again] or [press my right paw for the Next Step]?", 8, 9, "/medical8.wav"}, // 8 
    {"Last Step: Analysis [Press my left paw if you want to hear more] or [press my right paw to hear this procedure again]?", 10, 1, "/medical9.wav"}, //9 
    {"The sample will be sent to a lab, where experts will look for bad cancer cells. This takes a few days. [press my left paw to hear this step again] or [press my right paw to hear this procedure from the beginning]?", 10, 1,"/medical10.wav"}, //10 
};

String messages[] = {
    "Hi, from Mom!",
    "Hello, from friends!",
    "Thinking of you! From Grandma", 
    "Get Well Soon! From Teachers"
};

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

  File config = SD.open("/file_gains/values.txt", FILE_READ);
  while (config.available()) {
    char buffer[256];
    size_t len = config.readBytesUntil('\n', buffer, sizeof(buffer) - 1);
    buffer[len] = '\0';
    std::string line(buffer);
    std::size_t delimiter = line.find("\t");
    if (delimiter == std::string::npos) {
        continue;
    }
    std::string currFile(line.substr(0, delimiter));
    float filegain = std::stof(line.substr(delimiter + 1));
    fileGains.insert({currFile, filegain});
  }

  config.close();

  File file = root.openNextFile();

  // Format for each line in config file:
  // {FULLPATH (include / at beginning)}\t{GAIN}\n
  config = SD.open("/file_gains/values.txt", FILE_APPEND);

  while (file) {
    if (file.isDirectory()) {
      Serial.print("DIR : ");
      Serial.println(file.name());
    } else {
      Serial.print("FILE: ");
      Serial.print(file.name());
      Serial.print("  SIZE: ");
      Serial.println(file.size());
      int16_t buffer[1024];
      int maxAmplitude = 0;
      char* ptr = (char*) malloc(strlen(dirname) + strlen(file.name()) + 1);
      strcpy(ptr, dirname);
      strcat(ptr, file.name());
      std::string fullPath(ptr);
      free(ptr);
      if (auto search = fileGains.find(fullPath); search != fileGains.end()) {
        Serial.println("This file found in config.");
        file = root.openNextFile();
        continue;
      }
      Serial.println("Now adding file to config...");
      file.seek(sizeof(WAVHeader));
      while (file.available()) {
        int bytesToRead = min((int) file.available(), (int) sizeof(buffer));
        int samples = file.read((uint8_t*) buffer, bytesToRead) / sizeof(int16_t);
        for (int i = 0; i < samples; ++i) {
          if (abs(buffer[i]) > maxAmplitude) {
            maxAmplitude = abs(buffer[i]);
          }
        }
      }
      float gain = 32768.0 / maxAmplitude;
      fileGains[fullPath] = gain;
      Serial.printf("Gain: %f\n", gain);
      config.print((fullPath + "\t" + std::to_string(gain) + "\n").c_str());
    }
    file = root.openNextFile();
  }
  config.close();
}

void setup() {
    Serial.begin(115200);
    unsigned long start = millis();
    while(!Serial && millis() - start < 1000);  // Wait until Serial or until 1 second elapsed
        delay(10);
    ++bootCount;
    Serial.println("Boot number: " + String(bootCount));    
    // print_wakeup_reason();
    // esp_deep_sleep_enable_gpio_wakeup(WAKEUP_PIN_MASK, ESP_GPIO_WAKEUP_GPIO_LOW);
    // rtc_gpio_init(GPIO_NUM_4);
    // rtc_gpio_set_direction(GPIO_NUM_4, RTC_GPIO_MODE_INPUT_ONLY);
    // rtc_gpio_pullup_en(GPIO_NUM_4);
    // rtc_gpio_pulldown_dis(GPIO_NUM_4);
    initSinTable();
    setupI2S(SAMPLE_RATE, 2);
    sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    if (!SD.begin(SD_CS, sdSPI)) {
        Serial.println("Card Mount Failed");
        return;
    }

    Serial.println("SD card initialized.");
    Serial.println("Files on card:");

    listDir(SD, "/", 0);
    pinMode(motor1Pin, OUTPUT);
    pinMode(motor2Pin, OUTPUT);
    pinMode(leftFoot.PIN, INPUT_PULLUP);
    pinMode(rightFoot.PIN, INPUT_PULLUP);
    pinMode(leftHand.PIN, INPUT_PULLUP);
    pinMode(rightHand.PIN, INPUT_PULLUP);
    attachInterruptArg(leftFoot.PIN, isr, &leftFoot, FALLING);
    attachInterruptArg(rightFoot.PIN, isr, &rightFoot, FALLING);
    attachInterruptArg(leftHand.PIN, isr, &leftHand, FALLING);
    attachInterruptArg(rightHand.PIN, isr, &rightHand, FALLING);

    setVolume(volume);

    // Assign audio task to core 1
    xTaskCreatePinnedToCore(audioTask, "audioTask", 4096, NULL, 1, NULL, 1);
}
void loop() {
    if (breathingMode) {
        breathingExerciseNoBlock();

        // breathingExercise();
    }
    if (heartbeatMode) {
        heartbeatNonBlocking();
        
        // heartbeatOption(2));
    }
    if (leftHand.pressed) {
        leftHand.pressed = false;
        delay(5);
        if (digitalRead(leftHand.PIN) != LOW) {
            return;
        }
        buzzMotor();
        if (storytelling) {
            storyIndex = storyTree[storyIndex].optionA;
            Serial.println(storyTree[storyIndex].text);
            std::string nextFile(storyTree[storyIndex].audioFile);
            playWav(nextFile.c_str(), fileGains[nextFile]);
            Serial.println("Playback finished.");
        } else if (messagesOption){
            messageIndex = (messageIndex + 1) % messageCount;
            Serial.println(messages[messageIndex]);
            playWav("/test.wav", fileGains["/test.wav"]);
            Serial.println("Playback finished.");
        } else if (medicalStoryTime) {
            medicalIndex = medicalStory[medicalIndex].optionA;
            Serial.println(medicalStory[medicalIndex].text);
            std::string nextFile(medicalStory[medicalIndex].audioFile);
            playWav(nextFile.c_str(), fileGains[nextFile]);
            Serial.println("Playback finished.");
        }
    }
    if (rightHand.pressed) {
        rightHand.pressed = false;
        delay(5);
        if (digitalRead(rightHand.PIN) != LOW) {
            return;
        }
        buzzMotor();
        if (storytelling){
            storyIndex = storyTree[storyIndex].optionB;
            Serial.println(storyTree[storyIndex].text);
            std::string nextFile(storyTree[storyIndex].audioFile);
            playWav(nextFile.c_str(), fileGains[nextFile]);
            Serial.println("Playback finished.");
        } else if (messagesOption){
            messageIndex--;
            if (messageIndex < 0){
                messageIndex = messageCount - 1;
            }
            Serial.println(messages[messageIndex]);
            playWav("/test.wav", fileGains["/test.wav"]);
            Serial.println("Playback finished.");
        } else if (medicalStoryTime) {
            medicalIndex = medicalStory[medicalIndex].optionB;
            Serial.println(medicalStory[medicalIndex].text);
            std::string nextFile(medicalStory[medicalIndex].audioFile);
            playWav(nextFile.c_str(), fileGains[nextFile]);
            Serial.println("Playback finished.");
        }
    }
    if (leftFoot.pressed) {
        leftFoot.pressed = false;
        delay(5);
        if (digitalRead(leftFoot.PIN) != LOW) {
            return;
        }
        if (!breathingMode) buzzMotor();
        Serial.print("Left Foot.    ");
        setVolume(++volume);
        
        Serial.printf("Volume Level: %d\n", static_cast<int>(volume));
        // Play a short beep to confirm volume change (if not playing other audio)
        if (!isToneActive() && !isWavActive()) playTone(440.0, 200);
    }
    if (rightFoot.pressed) {
        rightFoot.pressed = false;
        delay(5);
        if (digitalRead(rightFoot.PIN) != LOW) {
            return;
        }
        buzzMotor();

        if (breathingMode) {
            breathState = IDLE;
            setHaptics(false);
        }
        if (heartbeatMode) {
            heartbeatState = HB_IDLE;
            setHaptics(false);
        }

        audioOptionIndex = (audioOptionIndex + 1) % actionsCount;
        Serial.print("Right Foot.   ");
        Serial.print("Now playing: ");
        Serial.println(audioActions[audioOptionIndex]);
        storytelling = (audioOptionIndex == 0);
        messagesOption = (audioOptionIndex == 2);
        medicalStoryTime = (audioOptionIndex == 4);
        breathingMode = (audioOptionIndex == 3);
        heartbeatMode = (audioOptionIndex == 5);
        if (storytelling) {
            Serial.println(storyTree[0].text);
            storyIndex = 0;
            std::string firstFile(storyTree[storyIndex].audioFile);
            playWav(firstFile.c_str(), fileGains[firstFile]);
            Serial.println("Playback finished.");
        } else if (audioOptionIndex == 1){
            Serial.println("play white noise audio");
            playWav("/test.wav", fileGains["/test.wav"]);
        } else if (audioOptionIndex == 3){
            Serial.println("play meditation audio");
        } else if (audioOptionIndex == 4){
            Serial.println("play medical walkthrough");
            Serial.println(medicalStory[0].text);
            medicalIndex = 0;
            std::string firstFile(medicalStory[medicalIndex].audioFile);
            playWav(firstFile.c_str(), fileGains[firstFile]);
            Serial.println("Playback finished.");
        } else if (audioOptionIndex == 5){
            Serial.println("play heartbeat");
        }
    }
}