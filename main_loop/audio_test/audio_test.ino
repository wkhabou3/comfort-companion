/*
Test file for audio subsystem features.

Tasks:
- Display all files on microSD card.
- Test tone generation (pitch and duration).
- Test MicroSD with WAV player by loading a wav file from card and playing it on speaker.
*/

#include "../audio.cpp"   // Ugly but Arduino compiler doesn't see the cpp file otherwise

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
  delay(2000);

  Serial.println("Begin audio test.");

  Serial.println("Pre-computing sine values...");
  initSinTable();

  Serial.println("Setting out I2S channel (44100Hz, stereo)...");
  setupI2S(SAMPLE_RATE, 2);

  Serial.println("Initializing SD card...");
  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, sdSPI)) {
    Serial.println("Card Mount Failed");
    return;
  }

  Serial.println("SD card initialized.");
  Serial.println("Files on card:");

  listDir(SD, "/", 0);

  Serial.println("Playing 440Hz tone for 2000ms...");
  playTone(440, 2000);
  Serial.println("Finished playing tone.");


  char *fileToPlay = "/test.wav";
  Serial.print("Playing file from SD card: ");
  Serial.println(fileToPlay);

  playWav(fileToPlay);

  Serial.println("Playback finished.");
  Serial.println("All tests passed!\n");
}

void loop() {
  // nothing
}