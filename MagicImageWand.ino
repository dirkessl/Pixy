/*
 Name:		MagicImageWand.ino
 Created:	12/18/2020 6:12:01 PM
 Author:	Martin
*/

#include "MagicImageWand.h"
#include "fonts.h"
#include <nvs_flash.h>

RTC_DATA_ATTR int nBootCount = 0;

// some forward references that Arduino IDE needs
int IRAM_ATTR readByte(bool clear);
void IRAM_ATTR ReadAndDisplayFile(bool doingFirstHalf);
uint16_t IRAM_ATTR readInt();
uint32_t IRAM_ATTR readLong();
void IRAM_ATTR FileSeekBuf(uint32_t place);
int FileCountOnly(int start = 0);

//static const char* TAG = "lightwand";
//esp_timer_cb_t oneshot_timer_callback(void* arg)
void IRAM_ATTR oneshot_LED_timer_callback(void* arg)
{
	bStripWaiting = false;
	//int64_t time_since_boot = esp_timer_get_time();
	//Serial.println("in isr");
	//ESP_LOGI(TAG, "One-shot timer called, time since boot: %lld us", time_since_boot);
}

constexpr int TFT_ENABLE = 4;
// use these to control the LCD brightness
const int freq = 5000;
const int ledChannel = 0;
const int resolution = 8;

void setup()
{
	Serial.begin(115200);
	delay(10);
	tft.init();
	// configure LCD PWM functionalitites
	pinMode(TFT_ENABLE, OUTPUT);
	digitalWrite(TFT_ENABLE, 1);
	ledcSetup(ledChannel, freq, resolution);
	// attach the channel to the GPIO to be controlled
	ledcAttachPin(TFT_ENABLE, ledChannel);
	SetDisplayBrightness(SystemInfo.nDisplayBrightness);
	tft.fillScreen(TFT_BLACK);
	tft.setRotation(3);
	//Serial.println("boot: " + String(nBootCount));
	CRotaryDialButton::begin(DIAL_A, DIAL_B, DIAL_BTN);
	setupSDcard();
	//listDir(SD, "/", 2, "");
	//gpio_set_direction((gpio_num_t)LED, GPIO_MODE_OUTPUT);
	//digitalWrite(LED, HIGH);
	gpio_set_direction((gpio_num_t)FRAMEBUTTON, GPIO_MODE_INPUT);
	gpio_set_pull_mode((gpio_num_t)FRAMEBUTTON, GPIO_PULLUP_ONLY);
	// init the onboard buttons
	gpio_set_direction(GPIO_NUM_0, GPIO_MODE_INPUT);
	gpio_set_pull_mode(GPIO_NUM_0, GPIO_PULLUP_ONLY);
	gpio_set_direction(GPIO_NUM_35, GPIO_MODE_INPUT);
	//gpio_set_pull_mode(GPIO_NUM_35, GPIO_PULLUP_ONLY); // not needed since there are no pullups on 35, they are input only

	oneshot_LED_timer_args = {
				oneshot_LED_timer_callback,
				/* argument specified here will be passed to timer callback function */
				(void*)0,
				ESP_TIMER_TASK,
				"one-shotLED"
	};
	esp_timer_create(&oneshot_LED_timer_args, &oneshot_LED_timer);

	//WiFi
	WiFi.softAP(ssid, password);
	IPAddress myIP = WiFi.softAPIP();
	// save for the menu system
	strncpy(localIpAddress, myIP.toString().c_str(), sizeof(localIpAddress));
	Serial.print("AP IP address: ");
	Serial.println(myIP);
	server.begin();
	Serial.println("Server started");
	server.on("/", HomePage);
	server.on("/download", File_Download);
	server.on("/upload", File_Upload);
	server.on("/settings", ShowSettings);
	server.on("/fupload", HTTP_POST, []() { server.send(200); }, handleFileUpload);
	///////////////////////////// End of Request commands
	server.begin();

	int width = tft.width();
	int height = tft.height();
	tft.fillScreen(TFT_BLACK);
	tft.setTextColor(SystemInfo.menuTextColor);
	rainbow_fill();
	if (nBootCount == 0)
	{
		tft.setTextColor(TFT_BLACK);
		tft.setFreeFont(&Irish_Grover_Regular_24);
		tft.drawRect(0, 0, width - 1, height - 1, SystemInfo.menuTextColor);
		tft.drawString("Magic Image Wand", 5, 10);
		tft.setFreeFont(&Dialog_bold_16);
		tft.drawString(String("Version ") + myVersion, 20, 70);
		tft.setTextSize(1);
		tft.drawString(__DATE__, 20, 90);
	}
	tft.setFreeFont(&Dialog_bold_16);

	if (SaveLoadSettings(false, true, false, true)) {
		if ((nBootCount == 0) && bAutoLoadSettings && gpio_get_level((gpio_num_t)DIAL_BTN)) {
			SaveLoadSettings(false, false, false, true);
			tft.drawString("Settings Loaded", 20, 110);
		}
	}
	else {
		// must not be anything there, so save it
		SaveLoadSettings(true, false, false, true);
	}
	tft.setFreeFont(&Dialog_bold_16);
	tft.setTextColor(SystemInfo.menuTextColor);

	menuPtr = new MenuInfo;
	MenuStack.push(menuPtr);
	MenuStack.top()->menu = MainMenu;
	MenuStack.top()->index = 0;
	MenuStack.top()->offset = 0;

	leds = (CRGB*)calloc(LedInfo.nTotalLeds, sizeof(*leds));
	FastLED.addLeds<NEOPIXEL, DATA_PIN1>(leds, 0, LedInfo.bSecondController ? LedInfo.nTotalLeds / 2 : LedInfo.nTotalLeds);
	//FastLED.addLeds<NEOPIXEL, DATA_PIN2>(leds, 0, NUM_LEDS);	// to test parallel second strip
	// create the second led controller
	if (LedInfo.bSecondController) {
		FastLED.addLeds<NEOPIXEL, DATA_PIN2>(leds, LedInfo.nTotalLeds / 2, LedInfo.nTotalLeds / 2);
		SetPixel(144, CRGB::Red);
		SetPixel(145, CRGB::Red);
		SetPixel(146, CRGB::Red);
		SetPixel(287, CRGB::Red);
		SetPixel(286, CRGB::Red);
		SetPixel(285, CRGB::Red);
		leds[144] = CRGB::Red;
		leds[145] = CRGB::Green;
		leds[146] = CRGB::Blue;
		FastLED.show();
	}
	//FastLED.setTemperature(whiteBalance);
	FastLED.setTemperature(CRGB(LedInfo.whiteBalance.r, LedInfo.whiteBalance.g, LedInfo.whiteBalance.b));
	FastLED.setBrightness(LedInfo.nLEDBrightness);
	FastLED.setMaxPowerInVoltsAndMilliamps(5, LedInfo.nStripMaxCurrent);
	if (nBootCount == 0) {
		//bool oldSecond = bSecondStrip;
		//bSecondStrip = true;
		// show 3 pixels on each end red and green, I had a strip that only showed 142 pixels, this will help detect that failure
		SetPixel(0, CRGB::Red);
		SetPixel(1, CRGB::Red);
		SetPixel(2, CRGB::Red);
		SetPixel(143, CRGB::Red);
		SetPixel(142, CRGB::Red);
		SetPixel(141, CRGB::Red);
		FastLED.show();
		delay(100);
		SetPixel(0, CRGB::Green);
		SetPixel(1, CRGB::Green);
		SetPixel(2, CRGB::Green);
		SetPixel(143, CRGB::Green);
		SetPixel(142, CRGB::Green);
		SetPixel(141, CRGB::Green);
		FastLED.show();
		delay(100);
		FastLED.clear(true);
		RainbowPulse();

		//fill_noise8(leds, 144, 2, 0, 10, 2, 0, 0, 10);
		//FastSPI_LED.show();
		//delay(5000);
		//bSecondStrip = oldSecond;
		//// Turn the LED on, then pause
		//SetPixel(0, CRGB::Red);
		//SetPixel(1, CRGB::Red);
		//SetPixel(4, CRGB::Green);
		//SetPixel(5, CRGB::Green);
		//SetPixel(8, CRGB::Blue);
		//SetPixel(9, CRGB::Blue);
		//SetPixel(12, CRGB::White);
		//SetPixel(13, CRGB::White);
		//SetPixel(NUM_LEDS - 0, CRGB::Red);
		//SetPixel(NUM_LEDS - 1, CRGB::Red);
		//SetPixel(NUM_LEDS - 4, CRGB::Green);
		//SetPixel(NUM_LEDS - 5, CRGB::Green);
		//SetPixel(NUM_LEDS - 8, CRGB::Blue);
		//SetPixel(NUM_LEDS - 9, CRGB::Blue);
		//SetPixel(NUM_LEDS - 12, CRGB::White);
		//SetPixel(NUM_LEDS - 13, CRGB::White);
		//SetPixel(0 + NUM_LEDS, CRGB::Red);
		//SetPixel(1 + NUM_LEDS, CRGB::Red);
		//SetPixel(4 + NUM_LEDS, CRGB::Green);
		//SetPixel(5 + NUM_LEDS, CRGB::Green);
		//SetPixel(8 + NUM_LEDS, CRGB::Blue);
		//SetPixel(9 + NUM_LEDS, CRGB::Blue);
		//SetPixel(12 + NUM_LEDS, CRGB::White);
		//SetPixel(13 + NUM_LEDS, CRGB::White);
		//for (int ix = 0; ix < 255; ix += 5) {
		//	FastLED.setBrightness(ix);
		//	FastLED.show();
		//	delayMicroseconds(50);
		//}
		//for (int ix = 255; ix >= 0; ix -= 5) {
		//	FastLED.setBrightness(ix);
		//	FastLED.show();
		//	delayMicroseconds(50);
		//}
		//delayMicroseconds(50);
		//FastLED.clear(true);
		//delayMicroseconds(50);
		//FastLED.setBrightness(nStripBrightness);
		//delay(50);
		//// Now turn the LED off
		//FastLED.clear(true);
		//delayMicroseconds(50);
		//// run a white dot up the display and back
		//for (int ix = 0; ix < STRIPLENGTH; ++ix) {
		//	SetPixel(ix, CRGB::White);
		//	if (ix)
		//		SetPixel(ix - 1, CRGB::Black);
		//	FastLED.show();
		//	delayMicroseconds(50);
		//}
		//for (int ix = STRIPLENGTH - 1; ix >= 0; --ix) {
		//	SetPixel(ix, CRGB::White);
		//	if (ix)
		//		SetPixel(ix + 1, CRGB::Black);
		//	FastLED.show();
		//	delayMicroseconds(50);
		//}
	}
	FastLED.clear(true);
	tft.fillScreen(TFT_BLACK);

	// wait for button release
	while (!digitalRead(DIAL_BTN))
		;
	delay(30);	// debounce
	while (!digitalRead(DIAL_BTN))
		;
	// clear the button buffer
	CRotaryDialButton::clear();
	if (!bSdCardValid) {
		DisplayCurrentFile();
		delay(1000);
		ToggleFilesBuiltin(NULL);
		tft.fillScreen(TFT_BLACK);
	}

	DisplayCurrentFile();
	/*
		analogSetCycles(8);                   // Set number of cycles per sample, default is 8 and provides an optimal result, range is 1 - 255
		analogSetSamples(1);                  // Set number of samples in the range, default is 1, it has an effect on sensitivity has been multiplied
		analogSetClockDiv(1);                 // Set the divider for the ADC clock, default is 1, range is 1 - 255
		analogSetAttenuation(ADC_11db);       // Sets the input attenuation for ALL ADC inputs, default is ADC_11db, range is ADC_0db, ADC_2_5db, ADC_6db, ADC_11db
		//analogSetPinAttenuation(36, ADC_11db); // Sets the input attenuation, default is ADC_11db, range is ADC_0db, ADC_2_5db, ADC_6db, ADC_11db
		analogSetPinAttenuation(37, ADC_11db);
		// ADC_0db provides no attenuation so IN/OUT = 1 / 1 an input of 3 volts remains at 3 volts before ADC measurement
		// ADC_2_5db provides an attenuation so that IN/OUT = 1 / 1.34 an input of 3 volts is reduced to 2.238 volts before ADC measurement
		// ADC_6db provides an attenuation so that IN/OUT = 1 / 2 an input of 3 volts is reduced to 1.500 volts before ADC measurement
		// ADC_11db provides an attenuation so that IN/OUT = 1 / 3.6 an input of 3 volts is reduced to 0.833 volts before ADC measurement
	//   adcAttachPin(VP);                     // Attach a pin to ADC (also clears any other analog mode that could be on), returns TRUE/FALSE result
	//   adcStart(VP);                         // Starts an ADC conversion on attached pin's bus
	//   adcBusy(VP);                          // Check if conversion on the pin's ADC bus is currently running, returns TRUE/FALSE result
	//   adcEnd(VP);

		//adcAttachPin(36);
		adcAttachPin(37);
	*/
}

void loop()
{
	static bool didsomething = false;
	didsomething = bSettingsMode ? HandleMenus() : HandleRunMode();
	if (!bSettingsMode && bControllerReboot) {
		WriteMessage("Rebooting due to\nLED controller change", false, 2000);
		SaveLoadSettings(true, true);
		ESP.restart();
	}
	server.handleClient();
	// wait for no keys
	if (didsomething) {
		didsomething = false;
		delay(1);
	}
	static bool bButton0 = false;
	// check preview button
	if (bButton0 && digitalRead(0)) {
		bButton0 = false;
	}
	if (!bButton0 && digitalRead(0) == 0) {
		// debounce
		delay(30);
		if (digitalRead(0) == 0) {
			ShowBmp(NULL);
			// kill the cancel flag
			bCancelRun = bCancelMacro = false;
			bButton0 = true;
			// wait for release
			while (digitalRead(0) == 0)
				;
			// restore the screen to what it was doing before the bmp display
			if (bSettingsMode) {
				ShowMenu(MenuStack.top()->menu);
			}
			else {
				tft.fillScreen(TFT_BLACK);
				DisplayCurrentFile(SystemInfo.bShowFolder);
			}
		}
	}
}

bool RunMenus(int button)
{
	// save this so we can see if we need to save a new changed value
	bool lastAutoLoadFlag = bAutoLoadSettings;
	// see if we got a menu match
	bool gotmatch = false;
	int menuix = 0;
	MenuInfo* oldMenu;
	bool bExit = false;
	for (int ix = 0; !gotmatch && MenuStack.top()->menu[ix].op != eTerminate; ++ix) {
		// see if this is one is valid
		if (!bMenuValid[ix]) {
			continue;
		}
		//Serial.println("menu button: " + String(button));
		if (button == BTN_SELECT && menuix == MenuStack.top()->index) {
			//Serial.println("got match " + String(menuix) + " " + String(MenuStack.top()->index));
			gotmatch = true;
			//Serial.println("clicked on menu");
			// got one, service it
			switch (MenuStack.top()->menu[ix].op) {
			case eText:
			case eTextInt:
			case eTextCurrentFile:
			case eBool:
				bMenuChanged = true;
				if (MenuStack.top()->menu[ix].function) {
					(*MenuStack.top()->menu[ix].function)(&MenuStack.top()->menu[ix]);
				}
				break;
			case eList:
				bMenuChanged = true;
				if (MenuStack.top()->menu[ix].function) {
					(*MenuStack.top()->menu[ix].function)(&MenuStack.top()->menu[ix]);
				}
				bExit = true;
				// if there is a value, set the min value in it
				if (MenuStack.top()->menu[ix].value) {
					*(int*)MenuStack.top()->menu[ix].value = MenuStack.top()->menu[ix].min;
				}
				break;
			case eMenu:
				if (MenuStack.top()->menu) {
					oldMenu = MenuStack.top();
					MenuStack.push(new MenuInfo);
					MenuStack.top()->menu = oldMenu->menu[ix].menu;
					bMenuChanged = true;
					MenuStack.top()->index = 0;
					MenuStack.top()->offset = 0;
					//Serial.println("change menu");
					// check if the new menu is an eList and if it has a value, if it does, set the index to it
					if (MenuStack.top()->menu->op == eList && MenuStack.top()->menu->value) {
						int ix = *(int*)MenuStack.top()->menu->value;
						MenuStack.top()->index = ix;
						// adjust offset if necessary
						if (ix > 4) {
							MenuStack.top()->offset = ix - 4;
						}
					}
				}
				break;
			case eBuiltinOptions: // find it in builtins
				if (BuiltInFiles[CurrentFileIndex].menu != NULL) {
					MenuStack.top()->index = MenuStack.top()->index;
					MenuStack.push(new MenuInfo);
					MenuStack.top()->menu = BuiltInFiles[CurrentFileIndex].menu;
					MenuStack.top()->index = 0;
					MenuStack.top()->offset = 0;
				}
				else {
					WriteMessage("No settings available for:\n" + String(BuiltInFiles[CurrentFileIndex].text));
				}
				bMenuChanged = true;
				break;
			case eExit: // go back a level
				bExit = true;
				break;
			case eReboot:
				WriteMessage("Rebooting in 2 seconds\nHold button for factory reset", false, 2000);
				ESP.restart();
				break;
			}
		}
		++menuix;
	}
	// if no match, and we are in a submenu, go back one level, or if bExit is set
	if (bExit || (!bMenuChanged && MenuStack.size() > 1)) {
		bMenuChanged = true;
		menuPtr = MenuStack.top();
		MenuStack.pop();
		delete menuPtr;
	}
	// see if the autoload flag changed
	if (bAutoLoadSettings != lastAutoLoadFlag) {
		// the flag is now true, so we should save the current settings
		SaveLoadSettings(true);
	}
}

#define MENU_LINES 7
// display the menu
// if MenuStack.top()->index is > MENU_LINES, then shift the lines up by enough to display them
// remember that we only have room for MENU_LINES lines
void ShowMenu(struct MenuItem* menu)
{
	MenuStack.top()->menucount = 0;
	int y = 0;
	int x = 0;
	char line[100]{};
	bool skip = false;
	// loop through the menu
	for (int menix = 0; menu->op != eTerminate; ++menu, ++menix) {
		// make sure menu valid vector is big enough
		if (bMenuValid.size() < menix + 1) {
			bMenuValid.resize(menix + 1);
		}
		bMenuValid[menix] = false;
		switch ((menu->op)) {
		case eIfEqual:
			// skip the next one if match, only booleans are handled so far
			skip = *(bool*)menu->value != (menu->min ? true : false);
			//Serial.println("ifequal test: skip: " + String(skip));
			break;
		case eElse:
			skip = !skip;
			break;
		case eEndif:
			skip = false;
			break;
		default:
			break;
		}
		if (skip) {
			bMenuValid[menix] = false;
			continue;
		}
		char line[100]{}, xtraline[100]{};
		// only displayable menu items should be in this switch
		line[0] = '\0';
		int val;
		bool exists;
		switch (menu->op) {
		case eTextInt:
		case eText:
		case eTextCurrentFile:
			bMenuValid[menix] = true;
			if (menu->value) {
				val = *(int*)menu->value;
				if (menu->op == eText) {
					sprintf(line, menu->text, (char*)(menu->value));
				}
				else if (menu->op == eTextInt) {
					sprintf(line, menu->text, (int)(val / pow10(menu->decimals)), val % (int)(pow10(menu->decimals)));
				}
			}
			else {
				if (menu->op == eTextCurrentFile) {
					sprintf(line, menu->text, MakeMIWFilename(FileNames[CurrentFileIndex], false).c_str());
				}
				else {
					strcpy(line, menu->text);
				}
			}
			// next line
			++y;
			break;
		case eList:
			bMenuValid[menix] = true;
			// the list of macro files
			// min holds the macro number
			val = menu->min;
			// see if the macro is there and append the text
			exists = SD.exists("/" + String(val) + ".miw");
			sprintf(line, menu->text, val, exists ? menu->on : menu->off);
			// next line
			++y;
			break;
		case eBool:
			bMenuValid[menix] = true;
			if (menu->value) {
				// clean extra bits, just in case
				bool* pb = (bool*)menu->value;
				//*pb &= 1;
				sprintf(line, menu->text, *pb ? menu->on : menu->off);
				//Serial.println("bool line: " + String(line));
			}
			else {
				strcpy(line, menu->text);
			}
			// increment displayable lines
			++y;
			break;
		case eBuiltinOptions:
			// for builtins only show if available
			if (BuiltInFiles[CurrentFileIndex].menu != NULL) {
				bMenuValid[menix] = true;
				sprintf(line, menu->text, BuiltInFiles[CurrentFileIndex].text);
				++y;
			}
			break;
		case eMenu:
		case eExit:
		case eReboot:
			bMenuValid[menix] = true;
			if (menu->value) {
				sprintf(xtraline, menu->text, *(int*)menu->value);
			}
			else {
				strcpy(xtraline, menu->text);
			}
			if (menu->op == eExit)
				sprintf(line, "%s%s", "-", xtraline);
			else
				sprintf(line, "%s%s", (menu->op == eReboot) ? "" : "+", xtraline);
			++y;
			//Serial.println("menu text4: " + String(line));
			break;
		default:
			break;
		}
		if (strlen(line) && y >= MenuStack.top()->offset) {
			DisplayMenuLine(y - 1, y - 1 - MenuStack.top()->offset, line);
		}
	}
	MenuStack.top()->menucount = y;
	// blank the rest of the lines
	for (int ix = y; ix < MENU_LINES; ++ix) {
		DisplayLine(ix, "");
	}
	// show line if menu has been scrolled
	if (MenuStack.top()->offset > 0)
		tft.fillTriangle(0, 0, 2, 0, 0, tft.fontHeight() / 3, TFT_DARKGREY);
		//tft.drawLine(0, 0, 5, 0, menuLineActiveColor);TFT_DARKGREY
	// show bottom line if last line is showing
	if (MenuStack.top()->offset + (MENU_LINES - 1) < MenuStack.top()->menucount - 1) {
		int ypos = tft.height() - 2 - tft.fontHeight() / 3;
		tft.fillTriangle(0, ypos, 2, ypos, 0, ypos - tft.fontHeight() / 3, TFT_DARKGREY);
	}
	//if (MenuStack.top()->offset + (MENU_LINES - 1) < MenuStack.top()->menucount - 1)
	//	tft.drawLine(0, tft.height() - 1, 5, tft.height() - 1, menuLineActiveColor);
	//else
	//	tft.drawLine(0, tft.height() - 1, 5, tft.height() - 1, TFT_BLACK);
	// see if we need to clean up the end, like when the menu shrank due to a choice
	int extra = MenuStack.top()->menucount - MenuStack.top()->offset - MENU_LINES;
	while (extra < 0) {
		DisplayLine(MENU_LINES + extra, "");
		++extra;
	}
}

// switch between SD and built-ins
void ToggleFilesBuiltin(MenuItem* menu)
{
	// clear filenames list
	FileNames.clear();
	bool lastval = SystemInfo.bShowBuiltInTests;
	int oldIndex = CurrentFileIndex;
	String oldFolder = currentFolder;
	if (menu != NULL) {
		ToggleBool(menu);
	}
	else {
		SystemInfo.bShowBuiltInTests = !SystemInfo.bShowBuiltInTests;
	}
	if (lastval != SystemInfo.bShowBuiltInTests) {
		if (SystemInfo.bShowBuiltInTests) {
			CurrentFileIndex = 0;
			for (int ix = 0; ix < sizeof(BuiltInFiles) / sizeof(*BuiltInFiles); ++ix) {
				// add each one
				FileNames.push_back(String(BuiltInFiles[ix].text));
			}
			currentFolder = "";
		}
		else {
			// read the SD
			currentFolder = lastFolder;
			GetFileNamesFromSD(currentFolder);
		}
	}
	// restore indexes
	CurrentFileIndex = lastFileIndex;
	lastFileIndex = oldIndex;
	currentFolder = lastFolder;
	lastFolder = oldFolder;
}

// toggle a boolean value
void ToggleBool(MenuItem* menu)
{
	bool* pb = (bool*)menu->value;
	*pb = !*pb;
	if (menu->change != NULL) {
		(*menu->change)(menu, -1);
	}
	//Serial.println("autoload: " + String(bAutoLoadSettings));
	//Serial.println("fixed time: " + String(bFixedTime));
}

// get integer values
void GetIntegerValue(MenuItem* menu)
{
	tft.fillScreen(TFT_BLACK);
	// -1 means to reset to original
	int stepSize = 1;
	int originalValue = *(int*)menu->value;
	//Serial.println("int: " + String(menu->text) + String(*(int*)menu->value));
	char line[50];
	CRotaryDialButton::Button button = BTN_NONE;
	bool done = false;
	const char* fmt = menu->decimals ? "%ld.%ld" : "%ld";
	char minstr[20], maxstr[20];
	sprintf(minstr, fmt, menu->min / (int)pow10(menu->decimals), menu->min % (int)pow10(menu->decimals));
	sprintf(maxstr, fmt, menu->max / (int)pow10(menu->decimals), menu->max % (int)pow10(menu->decimals));
	DisplayLine(1, String("Range: ") + String(minstr) + " to " + String(maxstr), SystemInfo.menuTextColor);
	DisplayLine(3, "Long Press to Accept", SystemInfo.menuTextColor);
	int oldVal = *(int*)menu->value;
	if (menu->change != NULL) {
		(*menu->change)(menu, 1);
	}
	do {
		//Serial.println("button: " + String(button));
		switch (button) {
		case BTN_LEFT:
			if (stepSize != -1)
				*(int*)menu->value -= stepSize;
			break;
		case BTN_RIGHT:
			if (stepSize != -1)
				*(int*)menu->value += stepSize;
			break;
		case BTN_SELECT:
			if (stepSize == -1) {
				stepSize = 1;
			}
			else {
				stepSize *= 10;
			}
			if (stepSize > (menu->max / 10)) {
				stepSize = -1;
			}
			break;
		case BTN_LONG:
			if (stepSize == -1) {
				*(int*)menu->value = originalValue;
				stepSize = 1;
			}
			else {
				done = true;
			}
			break;
		}
		// make sure within limits
		*(int*)menu->value = constrain(*(int*)menu->value, menu->min, menu->max);
		// show slider bar
		tft.fillRect(0, 2 * tft.fontHeight(), tft.width() - 1, 6, TFT_BLACK);
		DrawProgressBar(0, 2 * tft.fontHeight() + 4, tft.width() - 1, 12, map(*(int*)menu->value, menu->min, menu->max, 0, 100), true);
		sprintf(line, menu->text, *(int*)menu->value / (int)pow10(menu->decimals), *(int*)menu->value % (int)pow10(menu->decimals));
		DisplayLine(0, line, SystemInfo.menuTextColor);
		DisplayLine(4, stepSize == -1 ? "Reset: long press (Click +)" : "step: " + String(stepSize) + " (Click +)", SystemInfo.menuTextColor);
		if (menu->change != NULL && oldVal != *(int*)menu->value) {
			(*menu->change)(menu, 0);
			oldVal = *(int*)menu->value;
		}
		while (!done && (button = ReadButton()) == BTN_NONE) {
			delay(1);
		}
	} while (!done);
	if (menu->change != NULL) {
		(*menu->change)(menu, -1);
	}
}

void UpdateStripBrightness(MenuItem* menu, int flag)
{
	switch (flag) {
	case 1:		// first time
		for (int ix = 0; ix < 64; ++ix) {
			SetPixel(ix, CRGB::White);
		}
		FastLED.show();
		break;
	case 0:		// every change
		FastLED.setBrightness(*(int*)menu->value);
		FastLED.show();
		break;
	case -1:	// last time
		FastLED.clear(true);
		break;
	}
}

void UpdateStripWhiteBalanceR(MenuItem* menu, int flag)
{
	switch (flag) {
	case 1:		// first time
		for (int ix = 0; ix < 64; ++ix) {
			SetPixel(ix, CRGB::White);
		}
		FastLED.show();
		break;
	case 0:		// every change
		FastLED.setTemperature(CRGB(*(int*)menu->value, LedInfo.whiteBalance.g, LedInfo.whiteBalance.b));
		FastLED.show();
		break;
	case -1:	// last time
		FastLED.clear(true);
		break;
	}
}

void UpdateControllers(MenuItem* menu, int flag)
{
	WriteMessage("Reboot needed\nto take effect", false, 1000);
	bControllerReboot = true;
	if (LedInfo.bSecondController)
		LedInfo.nTotalLeds *= 2;
	else
		LedInfo.nTotalLeds /= 2;
}

void UpdateStripsMode(MenuItem* menu, int flag)
{
	static int lastmode;
	switch (flag) {
	case 1:		// first time
		lastmode = LedInfo.stripsMode;
		break;
	case 0:		// every change
		break;
	case -1:	// last time, expand but don't shrink
		if (lastmode != LedInfo.stripsMode) {
			WriteMessage("Reboot needed\nto take effect", false, 1000);
			bControllerReboot = true;
		}
		break;
	}
}

void UpdateTotalLeds(MenuItem* menu, int flag)
{
	static int lastcount;
	switch (flag) {
	case 1:		// first time
		lastcount = LedInfo.nTotalLeds;
		break;
	case 0:		// every change
		break;
	case -1:	// last time, expand but don't shrink
		if (LedInfo.nTotalLeds != lastcount) {
			WriteMessage("Reboot needed\nto take effect", false, 1000);
			bControllerReboot = true;
		}
		break;
	}
}

void UpdateStripWhiteBalanceG(MenuItem* menu, int flag)
{
	switch (flag) {
	case 1:		// first time
		for (int ix = 0; ix < 64; ++ix) {
			SetPixel(ix, CRGB::White);
		}
		FastLED.show();
		break;
	case 0:		// every change
		FastLED.setTemperature(CRGB(LedInfo.whiteBalance.r, *(int*)menu->value, LedInfo.whiteBalance.b));
		FastLED.show();
		break;
	case -1:	// last time
		FastLED.clear(true);
		break;
	}
}

void UpdateStripWhiteBalanceB(MenuItem* menu, int flag)
{
	switch (flag) {
	case 1:		// first time
		for (int ix = 0; ix < 64; ++ix) {
			SetPixel(ix, CRGB::White);
		}
		FastLED.show();
		break;
	case 0:		// every change
		FastLED.setTemperature(CRGB(LedInfo.whiteBalance.r, LedInfo.whiteBalance.g, *(int*)menu->value));
		FastLED.show();
		break;
	case -1:	// last time
		FastLED.clear(true);
		break;
	}
}

void UpdateDisplayBrightness(MenuItem* menu, int flag)
{
	// control LCD brightness
	SetDisplayBrightness(*(int*)menu->value);
}

void SetDisplayBrightness(int val)
{
	ledcWrite(ledChannel, map(val, 0, 100, 0, 255));
}

uint16_t ColorList[] = {
	//TFT_NAVY,
	//TFT_MAROON,
	//TFT_OLIVE,
	TFT_WHITE,
	TFT_LIGHTGREY,
	TFT_BLUE,
	TFT_SKYBLUE,
	TFT_CYAN,
	TFT_RED,
	TFT_BROWN,
	TFT_GREEN,
	TFT_MAGENTA,
	TFT_YELLOW,
	TFT_ORANGE,
	TFT_GREENYELLOW,
	TFT_GOLD,
	TFT_SILVER,
	TFT_VIOLET,
	TFT_PURPLE,
};

// find the color in the list
int FindMenuColor(uint16_t col)
{
	int ix;
	int colors = sizeof(ColorList) / sizeof(*ColorList);
	for (ix = 0; ix < colors; ++ix) {
		if (col == ColorList[ix])
			break;
	}
	return constrain(ix, 0, colors - 1);
}

void SetMenuColor(MenuItem* menu)
{
	int maxIndex = sizeof(ColorList) / sizeof(*ColorList) - 1;
	int colorIndex = FindMenuColor(SystemInfo.menuTextColor);
	tft.fillScreen(TFT_BLACK);
	DisplayLine(4, "Rotate change value", SystemInfo.menuTextColor);
	DisplayLine(5, "Long Press Exit", SystemInfo.menuTextColor);
	bool done = false;
	bool change = true;
	while (!done) {
		if (change) {
			DisplayLine(0, "Text Color", SystemInfo.menuTextColor);
			change = false;
		}
		switch (CRotaryDialButton::dequeue()) {
		case CRotaryDialButton::BTN_LONGPRESS:
			done = true;
			break;
		case CRotaryDialButton::BTN_RIGHT:
			change = true;
			colorIndex = ++colorIndex;
			break;
		case CRotaryDialButton::BTN_LEFT:
			change = true;
			colorIndex = --colorIndex;
			break;
		}
		colorIndex = constrain(colorIndex, 0, maxIndex);
		SystemInfo.menuTextColor = ColorList[colorIndex];
	}
}

// handle the menus
bool HandleMenus()
{
	if (bMenuChanged) {
		ShowMenu(MenuStack.top()->menu);
		bMenuChanged = false;
	}
	bool didsomething = true;
	CRotaryDialButton::Button button = ReadButton();
	int lastOffset = MenuStack.top()->offset;
	int lastMenu = MenuStack.top()->index;
	int lastMenuCount = MenuStack.top()->menucount;
	bool lastRecording = bRecordingMacro;
	switch (button) {
	case BTN_SELECT:
		RunMenus(button);
		bMenuChanged = true;
		break;
	case BTN_RIGHT:
		if (SystemInfo.bAllowMenuWrap || MenuStack.top()->index < MenuStack.top()->menucount - 1) {
			++MenuStack.top()->index;
		}
		if (MenuStack.top()->index >= MenuStack.top()->menucount) {
			MenuStack.top()->index = 0;
			bMenuChanged = true;
			MenuStack.top()->offset = 0;
		}
		// see if we need to scroll the menu
		if (MenuStack.top()->index - MenuStack.top()->offset > (MENU_LINES - 1)) {
			if (MenuStack.top()->offset < MenuStack.top()->menucount - MENU_LINES) {
				++MenuStack.top()->offset;
			}
		}
		break;
	case BTN_LEFT:
		if (SystemInfo.bAllowMenuWrap || MenuStack.top()->index > 0) {
			--MenuStack.top()->index;
		}
		if (MenuStack.top()->index < 0) {
			MenuStack.top()->index = MenuStack.top()->menucount - 1;
			bMenuChanged = true;
			MenuStack.top()->offset = MenuStack.top()->menucount - MENU_LINES;
		}
		// see if we need to adjust the offset
		if (MenuStack.top()->offset && MenuStack.top()->index < MenuStack.top()->offset) {
			--MenuStack.top()->offset;
		}
		break;
	case BTN_LONG:
		tft.fillScreen(TFT_BLACK);
		bSettingsMode = false;
		DisplayCurrentFile();
		bMenuChanged = true;
		break;
	default:
		didsomething = false;
		break;
	}
	// check some conditions that should redraw the menu
	if (lastMenu != MenuStack.top()->index || lastOffset != MenuStack.top()->offset) {
		bMenuChanged = true;
		//Serial.println("menu changed");
	}
	// see if the recording status changed
	if (lastRecording != bRecordingMacro) {
		MenuStack.top()->index = 0;
		MenuStack.top()->offset = 0;
		bMenuChanged = true;
	}
	return didsomething;
}

// handle keys in run mode
bool HandleRunMode()
{
	bool didsomething = true;
	int oldFileIndex = CurrentFileIndex;
	switch (ReadButton()) {
	case BTN_SELECT:
		bCancelRun = bCancelMacro = false;
		ProcessFileOrTest();
		break;
	case BTN_RIGHT:
		if (SystemInfo.bAllowMenuWrap || (CurrentFileIndex < FileNames.size() - 1))
			++CurrentFileIndex;
		if (CurrentFileIndex >= FileNames.size())
			CurrentFileIndex = 0;
		if (oldFileIndex != CurrentFileIndex)
			DisplayCurrentFile();
		break;
	case BTN_LEFT:
		if (SystemInfo.bAllowMenuWrap || (CurrentFileIndex > 0))
			--CurrentFileIndex;
		if (CurrentFileIndex < 0)
			CurrentFileIndex = FileNames.size() - 1;
		if (oldFileIndex != CurrentFileIndex)
			DisplayCurrentFile();
		break;
		//case btnShowFiles:
		//	bShowBuiltInTests = !bShowBuiltInTests;
		//	GetFileNamesFromSD(currentFolder);
		//	DisplayCurrentFile();
		//	break;
	case BTN_LONG:
		tft.fillScreen(TFT_BLACK);
		bSettingsMode = true;
		break;
	default:
		didsomething = false;
		break;
	}
	return didsomething;
}

// check buttons and return if one pressed
enum CRotaryDialButton::Button ReadButton()
{
	// check for the on board button 35
	static bool bButton35 = false;
	// check enter button, like longpress
	if (bButton35 && digitalRead(35)) {
		bButton35 = false;
	}
	if (!bButton35 && digitalRead(35) == 0) {
		// debounce
		delay(30);
		if (digitalRead(35) == 0) {
			bButton35 = true;
			CRotaryDialButton::pushButton(CRotaryDialButton::BTN_LONGPRESS);
		}
	}
	enum CRotaryDialButton::Button retValue = BTN_NONE;
	// read the next button, or NONE it none there
	retValue = CRotaryDialButton::dequeue();
	return retValue;
}

// just check for longpress and cancel if it was there
bool CheckCancel()
{
	// if it has been set, just return true
	if (bCancelRun || bCancelMacro)
		return true;
	int button = ReadButton();
	if (button) {
		if (button == BTN_LONG) {
			bCancelMacro = bCancelRun = true;
			return true;
		}
	}
	return false;
}

void setupSDcard()
{
	bSdCardValid = false;
#if USE_STANDARD_SD
	gpio_set_direction((gpio_num_t)SDcsPin, GPIO_MODE_OUTPUT);
	delay(50);
	//SPIClass(1);
	spiSDCard.begin(SDSckPin, SDMisoPin, SDMosiPin, SDcsPin);	// SCK,MISO,MOSI,CS
	delay(20);

	if (!SD.begin(SDcsPin, spiSDCard)) {
		//Serial.println("Card Mount Failed");
		return;
	}
	uint8_t cardType = SD.cardType();

	if (cardType == CARD_NONE) {
		//Serial.println("No SD card attached");
		return;
	}
#else
#define SD_CONFIG SdSpiConfig(SDcsPin, /*DEDICATED_SPI*/SHARED_SPI, SD_SCK_MHZ(10))
	SPI.begin(SDSckPin, SDMisoPin, SDMosiPin, SDcsPin);	// SCK,MISO,MOSI,CS
	if (!SD.begin(SD_CONFIG)) {
		Serial.println("SD initialization failed.");
		uint8_t err = SD.card()->errorCode();
		Serial.println("err: " + String(err));
		return;
	}
	//Serial.println("Mounted SD card");
	//SD.printFatType(&Serial);

	//uint64_t cardSize = (uint64_t)SD.clusterCount() * SD.bytesPerCluster() / (1024 * 1024 * 1024);
	//Serial.printf("SD Card Size: %llu GB\n", cardSize);
#endif
	bSdCardValid = GetFileNamesFromSD(currentFolder);
}

// return the pixel
CRGB IRAM_ATTR getRGBwithGamma() {
	if (LedInfo.bGammaCorrection) {
		b = gammaB[readByte(false)];
		g = gammaG[readByte(false)];
		r = gammaR[readByte(false)];
	}
	else {
		b = readByte(false);
		g = readByte(false);
		r = readByte(false);
	}
	return CRGB(r, g, b);
}

void fixRGBwithGamma(byte* rp, byte* gp, byte* bp) {
	if (LedInfo.bGammaCorrection) {
		*gp = gammaG[*gp];
		*bp = gammaB[*bp];
		*rp = gammaR[*rp];
	}
}

// up to 32 bouncing balls
void TestBouncingBalls() {
	CRGB colors[] = {
		CRGB::White,
		CRGB::Red,
		CRGB::Green,
		CRGB::Blue,
		CRGB::Yellow,
		CRGB::Cyan,
		CRGB::Magenta,
		CRGB::Grey,
		CRGB::RosyBrown,
		CRGB::RoyalBlue,
		CRGB::SaddleBrown,
		CRGB::Salmon,
		CRGB::SandyBrown,
		CRGB::SeaGreen,
		CRGB::Seashell,
		CRGB::Sienna,
		CRGB::Silver,
		CRGB::SkyBlue,
		CRGB::SlateBlue,
		CRGB::SlateGray,
		CRGB::SlateGrey,
		CRGB::Snow,
		CRGB::SpringGreen,
		CRGB::SteelBlue,
		CRGB::Tan,
		CRGB::Teal,
		CRGB::Thistle,
		CRGB::Tomato,
		CRGB::Turquoise,
		CRGB::Violet,
		CRGB::Wheat,
		CRGB::WhiteSmoke,
	};

	BouncingColoredBalls(BuiltinInfo.nBouncingBallsCount, colors);
	FastLED.clear(true);
}

void BouncingColoredBalls(int balls, CRGB colors[]) {
	time_t startsec = time(NULL);
	float Gravity = -9.81;
	int StartHeight = 1;

	float* Height = (float*)calloc(balls, sizeof(float));
	float* ImpactVelocity = (float*)calloc(balls, sizeof(float));
	float* TimeSinceLastBounce = (float*)calloc(balls, sizeof(float));
	int* Position = (int*)calloc(balls, sizeof(int));
	long* ClockTimeSinceLastBounce = (long*)calloc(balls, sizeof(long));
	float* Dampening = (float*)calloc(balls, sizeof(float));
	float ImpactVelocityStart = sqrt(-2 * Gravity * StartHeight);

	for (int i = 0; i < balls; i++) {
		ClockTimeSinceLastBounce[i] = millis();
		Height[i] = StartHeight;
		Position[i] = 0;
		ImpactVelocity[i] = ImpactVelocityStart;
		TimeSinceLastBounce[i] = 0;
		Dampening[i] = 0.90 - float(i) / pow(balls, 2);
	}

	long percent = 0;
	int colorChangeCounter = 0;
	bool done = false;
	while (!done) {
		if (CheckCancel()) {
			done = true;
			break;
		}
		for (int i = 0; i < balls; i++) {
			if (CheckCancel()) {
				done = true;
				break;
			}
			TimeSinceLastBounce[i] = millis() - ClockTimeSinceLastBounce[i];
			Height[i] = 0.5 * Gravity * pow(TimeSinceLastBounce[i] / BuiltinInfo.nBouncingBallsDecay, 2.0) + ImpactVelocity[i] * TimeSinceLastBounce[i] / BuiltinInfo.nBouncingBallsDecay;

			if (Height[i] < 0) {
				Height[i] = 0;
				ImpactVelocity[i] = Dampening[i] * ImpactVelocity[i];
				ClockTimeSinceLastBounce[i] = millis();

				if (ImpactVelocity[i] < 0.01) {
					ImpactVelocity[i] = ImpactVelocityStart;
				}
			}
			Position[i] = round(Height[i] * (LedInfo.nTotalLeds - 1) / StartHeight);
		}

		for (int i = 0; i < balls; i++) {
			int ix;
			if (CheckCancel()) {
				done = true;
				break;
			}
			ix = (i + BuiltinInfo.nBouncingBallsFirstColor) % 32;
			SetPixel(Position[i], colors[ix]);
		}
		if (BuiltinInfo.nBouncingBallsChangeColors && colorChangeCounter++ > (BuiltinInfo.nBouncingBallsChangeColors * 100)) {
			++BuiltinInfo.nBouncingBallsFirstColor;
			colorChangeCounter = 0;
		}
		ShowLeds();
		//FastLED.show();
		delayMicroseconds(50);
		FastLED.clear();
	}
	free(Height);
	free(ImpactVelocity);
	free(TimeSinceLastBounce);
	free(Position);
	free(ClockTimeSinceLastBounce);
	free(Dampening);
}

#define BARBERSIZE 10
#define BARBERCOUNT 40
void BarberPole()
{
	CRGB red, white, blue;
	byte r, g, b;
	r = 255, g = 0, b = 0;
	fixRGBwithGamma(&r, &g, &b);
	red = CRGB(r, g, b);
	r = 255, g = 255, b = 255;
	fixRGBwithGamma(&r, &g, &b);
	white = CRGB(r, g, b);
	r = 0, g = 0, b = 255;
	fixRGBwithGamma(&r, &g, &b);
	blue = CRGB(r, g, b);
	bool done = false;
	for (int loop = 0; !done; ++loop) {
		if (CheckCancel()) {
			done = true;
			break;
		}
		for (int ledIx = 0; ledIx < LedInfo.nTotalLeds; ++ledIx) {
			if (CheckCancel()) {
				done = true;
				break;
			}
			// figure out what color
			switch (((ledIx + loop) % BARBERCOUNT) / BARBERSIZE) {
			case 0: // red
				SetPixel(ledIx, red);
				break;
			case 1: // white
			case 3:
				SetPixel(ledIx, white);
				break;
			case 2: // blue
				SetPixel(ledIx, blue);
				break;
			}
		}
		ShowLeds();
		//FastLED.show();
		delay(ImgInfo.nFrameHold);
	}
}

// checkerboard
void CheckerBoard()
{
	int width = BuiltinInfo.nCheckboardBlackWidth + BuiltinInfo.nCheckboardWhiteWidth;
	int times = 0;
	CRGB color1 = CRGB::Black, color2 = CRGB::White;
	int addPixels = 0;
	bool done = false;
	while (!done) {
		for (int y = 0; y < LedInfo.nTotalLeds; ++y) {
			SetPixel(y, ((y + addPixels) % width) < BuiltinInfo.nCheckboardBlackWidth ? color1 : color2);
		}
		ShowLeds();
		//FastLED.show();
		int count = BuiltinInfo.nCheckerboardHoldframes;
		while (count-- > 0) {
			delay(ImgInfo.nFrameHold);
			if (CheckCancel()) {
				done = true;
				break;
			}
		}
		if (BuiltinInfo.bCheckerBoardAlternate && (times++ % 2)) {
			// swap colors
			CRGB temp = color1;
			color1 = color2;
			color2 = temp;
		}
		addPixels += BuiltinInfo.nCheckerboardAddPixels;
		if (CheckCancel()) {
			done = true;
			break;
		}
	}
}

void RandomBars()
{
	ShowRandomBars(BuiltinInfo.bRandomBarsBlacks);
}

// show random bars of lights with optional blacks between
void ShowRandomBars(bool blacks)
{
	time_t start = time(NULL);
	byte r, g, b;
	srand(millis());
	char line[40]{};
	bool done = false;
	for (int pass = 0; !done; ++pass) {
		if (blacks && (pass % 2)) {
			// odd numbers, clear
			FastLED.clear(true);
		}
		else {
			// even numbers, show bar
			r = random(0, 255);
			g = random(0, 255);
			b = random(0, 255);
			fixRGBwithGamma(&r, &g, &b);
			// fill the strip color
			FastLED.showColor(CRGB(r, g, b));
		}
		int count = BuiltinInfo.nRandomBarsHoldframes;
		while (count-- > 0) {
			delay(ImgInfo.nFrameHold);
			if (CheckCancel()) {
				done = true;
				break;
			}
		}
	}
}

// running dot
void RunningDot()
{
	for (int colorvalue = 0; colorvalue <= 3; ++colorvalue) {
		// RGBW
		byte r, g, b;
		switch (colorvalue) {
		case 0: // red
			r = 255;
			g = 0;
			b = 0;
			break;
		case 1: // green
			r = 0;
			g = 255;
			b = 0;
			break;
		case 2: // blue
			r = 0;
			g = 0;
			b = 255;
			break;
		case 3: // white
			r = 255;
			g = 255;
			b = 255;
			break;
		}
		fixRGBwithGamma(&r, &g, &b);
		char line[10]{};
		for (int ix = 0; ix < LedInfo.nTotalLeds; ++ix) {
			if (CheckCancel()) {
				break;
			}
			if (ix > 0) {
				SetPixel(ix - 1, CRGB::Black);
			}
			SetPixel(ix, CRGB(r, g, b));
			ShowLeds();
			//FastLED.show();
			delay(ImgInfo.nFrameHold);
		}
		// remember the last one, turn it off
		SetPixel(LedInfo.nTotalLeds - 1, CRGB::Black);
		ShowLeds();
		//FastLED.show();
	}
	FastLED.clear(true);
}

void OppositeRunningDots()
{
	for (int mode = 0; mode <= 3; ++mode) {
		if (CheckCancel())
			break;;
		// RGBW
		byte r, g, b;
		switch (mode) {
		case 0: // red
			r = 255;
			g = 0;
			b = 0;
			break;
		case 1: // green
			r = 0;
			g = 255;
			b = 0;
			break;
		case 2: // blue
			r = 0;
			g = 0;
			b = 255;
			break;
		case 3: // white
			r = 255;
			g = 255;
			b = 255;
			break;
		}
		fixRGBwithGamma(&r, &g, &b);
		for (int ix = 0; ix < LedInfo.nTotalLeds; ++ix) {
			if (CheckCancel())
				return;
			if (ix > 0) {
				SetPixel(ix - 1, CRGB::Black);
				SetPixel(LedInfo.nTotalLeds - ix + 1, CRGB::Black);
			}
			SetPixel(LedInfo.nTotalLeds - ix, CRGB(r, g, b));
			SetPixel(ix, CRGB(r, g, b));
			ShowLeds();
			//FastLED.show();
			delay(ImgInfo.nFrameHold);
		}
	}
}

void Sleep(MenuItem* menu)
{
	++nBootCount;
	//rtc_gpio_pullup_en(BTNPUSH);
	esp_sleep_enable_ext0_wakeup((gpio_num_t)DIAL_BTN, LOW);
	esp_deep_sleep_start();
}

void LightBar(MenuItem* menu)
{
	tft.fillScreen(TFT_BLACK);
	DisplayLine(0, "LED Light Bar", SystemInfo.menuTextColor);
	DisplayLine(3, "Rotate Dial to Change", SystemInfo.menuTextColor);
	DisplayLine(4, "Click to Set Operation", SystemInfo.menuTextColor);
	DisplayLedLightBar();
	FastLED.clear(true);
	// these were set by CheckCancel() in DisplayAllColor() and need to be cleared
	bCancelMacro = bCancelRun = false;
}

// utility for DisplayLedLightBar()
void FillLightBar()
{
	int offset = BuiltinInfo.bDisplayAllFromMiddle ? (LedInfo.nTotalLeds - BuiltinInfo.nDisplayAllPixelCount) / 2 : 0;
	if (!BuiltinInfo.bDisplayAllFromMiddle && ImgInfo.bUpsideDown)
		offset = LedInfo.nTotalLeds - BuiltinInfo.nDisplayAllPixelCount;
	FastLED.clear();
	for (int ix = 0; ix < BuiltinInfo.nDisplayAllPixelCount; ++ix) {
		SetPixel(ix + offset, BuiltinInfo.bDisplayAllRGB ? CRGB(BuiltinInfo.nDisplayAllRed, BuiltinInfo.nDisplayAllGreen, BuiltinInfo.nDisplayAllBlue) : CHSV(BuiltinInfo.nDisplayAllHue, BuiltinInfo.nDisplayAllSaturation, BuiltinInfo.nDisplayAllBrightness));
	}
	ShowLeds();
	//FastLED.show();
}

// Used LEDs as a light bar
void DisplayLedLightBar()
{
	DisplayLine(1, "");
	FillLightBar();
	// show until cancelled, but check for rotations of the knob
	CRotaryDialButton::Button btn;
	int what = 0;	// 0 for hue, 1 for saturation, 2 for brightness, 3 for pixels, 4 for increment
	int increment = 10;
	bool bChange = true;
	while (true) {
		if (bChange) {
			String line;
			switch (what) {
			case 0:
				if (BuiltinInfo.bDisplayAllRGB)
					line = "Red: " + String(BuiltinInfo.nDisplayAllRed);
				else
					line = "HUE: " + String(BuiltinInfo.nDisplayAllHue);
				break;
			case 1:
				if (BuiltinInfo.bDisplayAllRGB)
					line = "Green: " + String(BuiltinInfo.nDisplayAllGreen);
				else
					line = "Saturation: " + String(BuiltinInfo.nDisplayAllSaturation);
				break;
			case 2:
				if (BuiltinInfo.bDisplayAllRGB)
					line = "Blue: " + String(BuiltinInfo.nDisplayAllBlue);
				else
					line = "Brightness: " + String(BuiltinInfo.nDisplayAllBrightness);
				break;
			case 3:
				line = "Pixels: " + String(BuiltinInfo.nDisplayAllPixelCount);
				break;
			case 4:
				line = "From: " + String((BuiltinInfo.bDisplayAllFromMiddle ? "Middle" : "End"));
				break;
			case 5:
				line = " (step size: " + String(increment) + ")";
				break;
			}
			DisplayLine(2, line, SystemInfo.menuTextColor);
		}
		btn = ReadButton();
		bChange = true;
		switch (btn) {
		case BTN_NONE:
			bChange = false;
			break;
		case BTN_RIGHT:
			switch (what) {
			case 0:
				if (BuiltinInfo.bDisplayAllRGB)
					BuiltinInfo.nDisplayAllRed += increment;
				else
					BuiltinInfo.nDisplayAllHue += increment;
				break;
			case 1:
				if (BuiltinInfo.bDisplayAllRGB)
					BuiltinInfo.nDisplayAllGreen += increment;
				else
					BuiltinInfo.nDisplayAllSaturation += increment;
				break;
			case 2:
				if (BuiltinInfo.bDisplayAllRGB)
					BuiltinInfo.nDisplayAllBlue += increment;
				else
					BuiltinInfo.nDisplayAllBrightness += increment;
				break;
			case 3:
				BuiltinInfo.nDisplayAllPixelCount += increment;
				break;
			case 4:
				BuiltinInfo.bDisplayAllFromMiddle = true;
				break;
			case 5:
				increment *= 10;
				break;
			}
			break;
		case BTN_LEFT:
			switch (what) {
			case 0:
				if (BuiltinInfo.bDisplayAllRGB)
					BuiltinInfo.nDisplayAllRed -= increment;
				else
					BuiltinInfo.nDisplayAllHue -= increment;
				break;
			case 1:
				if (BuiltinInfo.bDisplayAllRGB)
					BuiltinInfo.nDisplayAllGreen -= increment;
				else
					BuiltinInfo.nDisplayAllSaturation -= increment;
				break;
			case 2:
				if (BuiltinInfo.bDisplayAllRGB)
					BuiltinInfo.nDisplayAllBlue -= increment;
				else
					BuiltinInfo.nDisplayAllBrightness -= increment;
				break;
			case 3:
				BuiltinInfo.nDisplayAllPixelCount -= increment;
				break;
			case 4:
				BuiltinInfo.bDisplayAllFromMiddle = false;
				break;
			case 5:
				increment /= 10;
				break;
			}
			break;
		case BTN_SELECT:
			// switch to the next selection, wrapping around if necessary
			what = ++what % 6;
			break;
		case BTN_LONG:
			// put it back, we don't want it
			CRotaryDialButton::pushButton(btn);
			break;
		}
		if (CheckCancel())
			return;
		if (bChange) {
			BuiltinInfo.nDisplayAllPixelCount = constrain(BuiltinInfo.nDisplayAllPixelCount, 1, LedInfo.nTotalLeds);
			increment = constrain(increment, 1, 100);
			if (BuiltinInfo.bDisplayAllRGB) {
				if (BuiltinInfo.bAllowRollover) {
					if (BuiltinInfo.nDisplayAllRed < 0)
						BuiltinInfo.nDisplayAllRed = RollDownRollOver(increment);
					if (BuiltinInfo.nDisplayAllRed > 255)
						BuiltinInfo.nDisplayAllRed = 0;
					if (BuiltinInfo.nDisplayAllGreen < 0)
						BuiltinInfo.nDisplayAllGreen = RollDownRollOver(increment);
					if (BuiltinInfo.nDisplayAllGreen > 255)
						BuiltinInfo.nDisplayAllGreen = 0;
					if (BuiltinInfo.nDisplayAllBlue < 0)
						BuiltinInfo.nDisplayAllBlue = RollDownRollOver(increment);
					if (BuiltinInfo.nDisplayAllBlue > 255)
						BuiltinInfo.nDisplayAllBlue = 0;
				}
				else {
					BuiltinInfo.nDisplayAllRed = constrain(BuiltinInfo.nDisplayAllRed, 0, 255);
					BuiltinInfo.nDisplayAllGreen = constrain(BuiltinInfo.nDisplayAllGreen, 0, 255);
					BuiltinInfo.nDisplayAllBlue = constrain(BuiltinInfo.nDisplayAllBlue, 0, 255);
				}
				FillLightBar();
			}
			else {
				if (BuiltinInfo.bAllowRollover) {
					if (BuiltinInfo.nDisplayAllHue < 0)
						BuiltinInfo.nDisplayAllHue = RollDownRollOver(increment);
					if (BuiltinInfo.nDisplayAllHue > 255)
						BuiltinInfo.nDisplayAllHue = 0;
					if (BuiltinInfo.nDisplayAllSaturation < 0)
						BuiltinInfo.nDisplayAllSaturation = RollDownRollOver(increment);
					if (BuiltinInfo.nDisplayAllSaturation > 255)
						BuiltinInfo.nDisplayAllSaturation = 0;
				}
				else {
					BuiltinInfo.nDisplayAllHue = constrain(BuiltinInfo.nDisplayAllHue, 0, 255);
					BuiltinInfo.nDisplayAllSaturation = constrain(BuiltinInfo.nDisplayAllSaturation, 0, 255);
				}
				BuiltinInfo.nDisplayAllBrightness = constrain(BuiltinInfo.nDisplayAllBrightness, 0, 255);
				FillLightBar();
			}
		}
		delay(10);
	}
}

// handle rollover when -ve
// inc 1 gives 255, inc 10 gives 250, inc 100 gives 200
int RollDownRollOver(int inc)
{
	if (inc == 1)
		return 255;
	int retval = 256;
	retval -= retval % inc;
	return retval;
}

void TestTwinkle() {
	TwinkleRandom(ImgInfo.nFrameHold, BuiltinInfo.bTwinkleOnlyOne);
}
void TwinkleRandom(int SpeedDelay, boolean OnlyOne) {
	time_t start = time(NULL);
	bool done = false;
	while (!done) {
		SetPixel(random(LedInfo.nTotalLeds), CRGB(random(0, 255), random(0, 255), random(0, 255)));
		ShowLeds();
		//FastLED.show();
		delay(SpeedDelay);
		if (OnlyOne) {
			FastLED.clear(true);
		}
		if (CheckCancel()) {
			done = true;
			break;
		}
	}
}

void TestCylon()
{
	CylonBounce(BuiltinInfo.nCylonEyeRed, BuiltinInfo.nCylonEyeGreen, BuiltinInfo.nCylonEyeBlue, BuiltinInfo.nCylonEyeSize, ImgInfo.nFrameHold, 50);
}
void CylonBounce(byte red, byte green, byte blue, int EyeSize, int SpeedDelay, int ReturnDelay)
{
	for (int i = 0; i < LedInfo.nTotalLeds - EyeSize - 2; i++) {
		if (CheckCancel()) {
			break;
		}
		FastLED.clear();
		SetPixel(i, CRGB(red / 10, green / 10, blue / 10));
		for (int j = 1; j <= EyeSize; j++) {
			SetPixel(i + j, CRGB(red, green, blue));
		}
		SetPixel(i + EyeSize + 1, CRGB(red / 10, green / 10, blue / 10));
		ShowLeds();
		//FastLED.show();
		delay(SpeedDelay);
	}
	delay(ReturnDelay);
	for (int i = LedInfo.nTotalLeds - EyeSize - 2; i > 0; i--) {
		if (CheckCancel()) {
			break;
		}
		FastLED.clear();
		SetPixel(i, CRGB(red / 10, green / 10, blue / 10));
		for (int j = 1; j <= EyeSize; j++) {
			SetPixel(i + j, CRGB(red, green, blue));
		}
		SetPixel(i + EyeSize + 1, CRGB(red / 10, green / 10, blue / 10));
		ShowLeds();
		//FastLED.show();
		delay(SpeedDelay);
	}
	FastLED.clear(true);
}

void TestMeteor() {
	meteorRain(BuiltinInfo.nMeteorRed, BuiltinInfo.nMeteorGreen, BuiltinInfo.nMeteorBlue, BuiltinInfo.nMeteorSize, 64, true, 30);
}

void meteorRain(byte red, byte green, byte blue, byte meteorSize, byte meteorTrailDecay, boolean meteorRandomDecay, int SpeedDelay)
{
	FastLED.clear(true);

	for (int i = 0; i < LedInfo.nTotalLeds + LedInfo.nTotalLeds; i++) {
		if (CheckCancel())
			break;;
		// fade brightness all LEDs one step
		for (int j = 0; j < LedInfo.nTotalLeds; j++) {
			if (CheckCancel())
				break;
			if ((!meteorRandomDecay) || (random(10) > 5)) {
				fadeToBlack(j, meteorTrailDecay);
			}
		}
		// draw meteor
		for (int j = 0; j < meteorSize; j++) {
			if (CheckCancel())
				break;
			if ((i - j < LedInfo.nTotalLeds) && (i - j >= 0)) {
				SetPixel(i - j, CRGB(red, green, blue));
			}
		}
		ShowLeds();
		//FastLED.show();
		delay(SpeedDelay);
	}
}

void TestConfetti()
{
	time_t start = time(NULL);
	BuiltinInfo.gHue = 0;
	bool done = false;
	while (!done) {
		EVERY_N_MILLISECONDS(ImgInfo.nFrameHold) {
			if (BuiltinInfo.bConfettiCycleHue)
				++BuiltinInfo.gHue;
			confetti();
			ShowLeds();
			//FastLED.show();
		}
		if (CheckCancel()) {
			done = true;
			break;
		}
	}
	// wait for timeout so strip will be blank
	delay(100);
}

void confetti()
{
	// random colored speckles that blink in and fade smoothly
	fadeToBlackBy(leds, LedInfo.nTotalLeds, 10);
	int pos = random16(LedInfo.nTotalLeds);
	leds[pos] += CHSV(BuiltinInfo.gHue + random8(64), 200, 255);
}

void TestJuggle()
{
	bool done = false;
	while (!done) {
		EVERY_N_MILLISECONDS(ImgInfo.nFrameHold) {
			juggle();
			ShowLeds();
			//FastLED.show();
		}
		if (CheckCancel()) {
			done = true;
			break;
		}
	}
}

void juggle()
{
	// eight colored dots, weaving in and out of sync with each other
	fadeToBlackBy(leds, LedInfo.nTotalLeds, 20);
	byte dothue = 0;
	uint16_t index;
	for (int i = 0; i < 8; i++) {
		index = beatsin16(i + 7, 0, LedInfo.nTotalLeds);
		// use AdjustStripIndex to get the right one
		SetPixel(index, leds[AdjustStripIndex(index)] | CHSV(dothue, 255, 255));
		//leds[beatsin16(i + 7, 0, STRIPLENGTH)] |= CHSV(dothue, 255, 255);
		dothue += 32;
	}
}

void TestSine()
{
	BuiltinInfo.gHue = BuiltinInfo.nSineStartingHue;
	bool done = false;
	while (!done) {
		EVERY_N_MILLISECONDS(ImgInfo.nFrameHold) {
			sinelon();
			ShowLeds();
			//FastLED.show();
		}
		if (CheckCancel()) {
			done = true;
			break;
		}
	}
}
void sinelon()
{
	// a colored dot sweeping back and forth, with fading trails
	fadeToBlackBy(leds, LedInfo.nTotalLeds, 20);
	int pos = beatsin16(BuiltinInfo.nSineSpeed, 0, LedInfo.nTotalLeds);
	leds[AdjustStripIndex(pos)] += CHSV(BuiltinInfo.gHue, 255, 192);
	if (BuiltinInfo.bSineCycleHue)
		++BuiltinInfo.gHue;
}

void TestBpm()
{
	BuiltinInfo.gHue = 0;
	bool done = false;
	while (!done) {
		EVERY_N_MILLISECONDS(ImgInfo.nFrameHold) {
			bpm();
			ShowLeds();
			//FastLED.show();
		}
		if (CheckCancel()) {
			done = true;
			break;
		}
	}
}

void bpm()
{
	// colored stripes pulsing at a defined Beats-Per-Minute (BPM)
	CRGBPalette16 palette = PartyColors_p;
	uint8_t beat = beatsin8(BuiltinInfo.nBpmBeatsPerMinute, 64, 255);
	for (int i = 0; i < LedInfo.nTotalLeds; i++) { //9948
		SetPixel(i, ColorFromPalette(palette, BuiltinInfo.gHue + (i * 2), beat - BuiltinInfo.gHue + (i * 10)));
	}
	if (BuiltinInfo.bBpmCycleHue)
		++BuiltinInfo.gHue;
}

void FillRainbow(struct CRGB* pFirstLED, int numToFill,
	uint8_t initialhue,
	int deltahue)
{
	CHSV hsv;
	hsv.hue = initialhue;
	hsv.val = 255;
	hsv.sat = 240;
	for (int i = 0; i < numToFill; i++) {
		pFirstLED[AdjustStripIndex(i)] = hsv;
		hsv.hue += deltahue;
	}
}

void TestRainbow()
{
	BuiltinInfo.gHue = BuiltinInfo.nRainbowInitialHue;
	FillRainbow(leds, LedInfo.nTotalLeds, BuiltinInfo.gHue, BuiltinInfo.nRainbowHueDelta);
	FadeInOut(BuiltinInfo.nRainbowFadeTime * 100, true);
	bool done = false;
	while (!done) {
		EVERY_N_MILLISECONDS(ImgInfo.nFrameHold) {
			if (BuiltinInfo.bRainbowCycleHue)
				++BuiltinInfo.gHue;
			FillRainbow(leds, LedInfo.nTotalLeds, BuiltinInfo.gHue, BuiltinInfo.nRainbowHueDelta);
			if (BuiltinInfo.bRainbowAddGlitter)
				addGlitter(80);
			ShowLeds();
			//FastLED.show();
		}
		if (CheckCancel()) {
			done = true;
			FastLED.setBrightness(LedInfo.nLEDBrightness);
			break;
		}
	}
	FadeInOut(BuiltinInfo.nRainbowFadeTime * 100, false);
	FastLED.setBrightness(LedInfo.nLEDBrightness);
}

// create a user defined stripe set
// it consists of a list of stripes, each of which have a width and color
// there can be up to 10 of these
#define NUM_STRIPES 20
struct {
	int start;
	int length;
	CHSV color;
} Stripes[NUM_STRIPES];

void TestStripes()
{
	FastLED.setBrightness(LedInfo.nLEDBrightness);
	// let's fill in some data
	for (int ix = 0; ix < NUM_STRIPES; ++ix) {
		Stripes[ix].start = ix * 20;
		Stripes[ix].length = 12;
		Stripes[ix].color = CHSV(0, 0, 255);
	}
	int pix = 0;	// pixel address
	FastLED.clear(true);
	for (int ix = 0; ix < NUM_STRIPES; ++ix) {
		pix = Stripes[ix].start;
		// fill in each block of pixels
		for (int len = 0; len < Stripes[ix].length; ++len) {
			SetPixel(pix++, CRGB(Stripes[ix].color));
		}
	}
	ShowLeds();
	//FastLED.show();
	bool done = false;
	while (!done) {
		if (CheckCancel()) {
			done = true;
			break;
		}
		delay(1000);
	}
}

// alternating white and black lines
void TestLines()
{
	FastLED.setBrightness(LedInfo.nLEDBrightness);
	FastLED.clear(true);
	bool bWhite = true;
	for (int pix = 0; pix < LedInfo.nTotalLeds; ++pix) {
		// fill in each block of pixels
		for (int len = 0; len < (bWhite ? BuiltinInfo.nLinesWhite : BuiltinInfo.nLinesBlack); ++len) {
			SetPixel(pix++, bWhite ? CRGB::White : CRGB::Black);
		}
		bWhite = !bWhite;
	}
	ShowLeds();
	//FastLED.show();
	bool done = false;
	while (!done) {
		if (CheckCancel()) {
			done = true;
			break;
		}
		delay(1000);
		// might make this work to toggle blacks and whites eventually
		//for (int ix = 0; ix < STRIPLENGTH; ++ix) {
		//	leds[ix] = (leds[ix] == CRGB::White) ? CRGB::Black : CRGB::White;
		//}
		ShowLeds();
		//FastLED.show();
	}
	FastLED.clear(true);
}

// time is in mSec
void FadeInOut(int time, bool in)
{
	if (in) {
		for (int i = 0; i <= LedInfo.nLEDBrightness; ++i) {
			FastLED.setBrightness(i);
			ShowLeds();
			//FastLED.show();
			delay(time / LedInfo.nLEDBrightness);
		}
	}
	else {
		for (int i = LedInfo.nLEDBrightness; i >= 0; --i) {
			FastLED.setBrightness(i);
			ShowLeds();
			//FastLED.show();
			delay(time / LedInfo.nLEDBrightness);
		}
	}
}

void addGlitter(fract8 chanceOfGlitter)
{
	if (random8() < chanceOfGlitter) {
		leds[random16(LedInfo.nTotalLeds)] += CRGB::White;
	}
}

void fadeToBlack(int ledNo, byte fadeValue) {
	// FastLED
	leds[ledNo].fadeToBlackBy(fadeValue);
}

// run file or built-in
void ProcessFileOrTest()
{
	String line;
	// let's see if this is a folder command
	String tmp = FileNames[CurrentFileIndex];
	if (tmp[0] == NEXT_FOLDER_CHAR) {
		FileIndexStack.push(CurrentFileIndex);
		tmp = tmp.substring(1);
		// change folder, reload files
		currentFolder += tmp + "/";
		GetFileNamesFromSD(currentFolder);
		DisplayCurrentFile();
		return;
	}
	else if (tmp[0] == PREVIOUS_FOLDER_CHAR) {
		tmp = currentFolder.substring(0, currentFolder.length() - 1);
		tmp = tmp.substring(0, tmp.lastIndexOf("/") + 1);
		// change folder, reload files
		currentFolder = tmp;
		GetFileNamesFromSD(currentFolder);
		CurrentFileIndex = FileIndexStack.top();
		FileIndexStack.pop();
		DisplayCurrentFile();
		return;
	}
	if (bRecordingMacro) {
		strcpy(FileToShow, FileNames[CurrentFileIndex].c_str());
		// tag the start time
		recordingTimeStart = time(NULL);
		WriteOrDeleteConfigFile(String(ImgInfo.nCurrentMacro), false, false);
	}
	bIsRunning = true;
	// clear the rest of the lines
	for (int ix = 1; ix < MENU_LINES; ++ix)
		DisplayLine(ix, "");
	//DisplayCurrentFile();
	if (ImgInfo.startDelay) {
		// set a timer
		nTimerSeconds = ImgInfo.startDelay;
		while (nTimerSeconds && !CheckCancel()) {
			line = "Start Delay: " + String(nTimerSeconds / 10) + "." + String(nTimerSeconds % 10);
			DisplayLine(2, line, SystemInfo.menuTextColor);
			delay(100);
			--nTimerSeconds;
		}
		DisplayLine(3, "");
	}
	int chainCount = ImgInfo.bChainFiles ? FileCountOnly(CurrentFileIndex) : 1;
	int chainFileCount = chainCount;
	int chainRepeatCount = ImgInfo.bChainFiles ? ImgInfo.nChainRepeats : 1;
	int lastFileIndex = CurrentFileIndex;
	// don't allow chaining for built-ins, although maybe we should
	if (SystemInfo.bShowBuiltInTests) {
		chainCount = 1;
		chainRepeatCount = 1;
	}
	// set the basic LED info
	FastLED.setTemperature(CRGB(LedInfo.whiteBalance.r, LedInfo.whiteBalance.g, LedInfo.whiteBalance.b));
	FastLED.setBrightness(LedInfo.nLEDBrightness);
	FastLED.setMaxPowerInVoltsAndMilliamps(5, LedInfo.nStripMaxCurrent);
	line = "";
	while (chainRepeatCount-- > 0) {
		while (chainCount-- > 0) {
			DisplayCurrentFile();
			if (ImgInfo.bChainFiles && !SystemInfo.bShowBuiltInTests) {
				line = "Remaining: " + String(chainCount + 1);
				DisplayLine(4, line, SystemInfo.menuTextColor);
				if (CurrentFileIndex < chainFileCount - 1) {
					line = "Next: " + FileNames[CurrentFileIndex + 1];
				}
				else {
					line = "";
				}
				DisplayLine(5, line, SystemInfo.menuTextColor);
			}
			line = "";
			// process the repeats and waits for each file in the list
			for (nRepeatsLeft = ImgInfo.repeatCount; nRepeatsLeft > 0; nRepeatsLeft--) {
				// fill the progress bar
				if (!SystemInfo.bShowBuiltInTests)
					ShowProgressBar(0);
				if (ImgInfo.repeatCount > 1) {
					line = "Repeats: " + String(nRepeatsLeft) + " ";
				}
				if (!SystemInfo.bShowBuiltInTests && ImgInfo.nChainRepeats > 1) {
					line += "Chains: " + String(chainRepeatCount + 1);
				}
				DisplayLine(3, line, SystemInfo.menuTextColor);
				if (SystemInfo.bShowBuiltInTests) {
					DisplayLine(4, "Running (long cancel)", SystemInfo.menuTextColor);
					// run the test
					(*BuiltInFiles[CurrentFileIndex].function)();
				}
				else {
					if (ImgInfo.nRepeatCountMacro > 1 && bRunningMacro) {
						DisplayLine(4, String("Macro Repeats: ") + String(nMacroRepeatsLeft), SystemInfo.menuTextColor);
					}
					// output the file
					SendFile(FileNames[CurrentFileIndex]);
				}
				if (bCancelRun) {
					break;
				}
				if (!SystemInfo.bShowBuiltInTests)
					ShowProgressBar(0);
				if (nRepeatsLeft > 1) {
					if (ImgInfo.repeatDelay) {
						FastLED.clear(true);
						// start timer
						nTimerSeconds = ImgInfo.repeatDelay;
						while (nTimerSeconds > 0 && !CheckCancel()) {
							line = "Repeat Delay: " + String(nTimerSeconds / 10) + "." + String(nTimerSeconds % 10);
							DisplayLine(2, line, SystemInfo.menuTextColor);
							line = "";
							delay(100);
							--nTimerSeconds;
						}
						//DisplayLine(3, "");
					}
				}
			}
			if (bCancelRun) {
				chainCount = 0;
				break;
			}
			if (SystemInfo.bShowBuiltInTests)
				break;
			// see if we are chaining, if so, get the next file, if a folder we're done
			if (ImgInfo.bChainFiles) {
				// grab the next file
				if (CurrentFileIndex < FileNames.size() - 1)
					++CurrentFileIndex;
				if (IsFolder(CurrentFileIndex))
					break;
				// handle any chain delay
				for (int dly = ImgInfo.nChainDelay; dly > 0 && !CheckCancel(); --dly) {
					line = "Chain Delay: " + String(dly / 10) + "." + String(dly % 10);
					DisplayLine(2, line, SystemInfo.menuTextColor);
					delay(100);
				}
				// check for chain wait for keypress
				if (chainCount && ImgInfo.bChainWaitKey) {
					DisplayLine(2, "Click: " + FileNames[CurrentFileIndex], SystemInfo.menuTextColor);
					bool waitNext = true;
					int wbtn;
					while (waitNext) {
						delay(10);
						wbtn = ReadButton();
						if (wbtn == BTN_NONE)
							continue;
						if (wbtn == BTN_LONG) {
							CRotaryDialButton::pushButton(CRotaryDialButton::BTN_LONGPRESS);
						}
						else {
							waitNext = false;
						}
						if (CheckCancel()) {
							waitNext = false;
						}
					}
				}
			}
			line = "";
			// clear
			FastLED.clear(true);
		}
		if (bCancelRun) {
			chainCount = 0;
			chainRepeatCount = 0;
			bCancelRun = false;
			break;
		}
		// start again
		CurrentFileIndex = lastFileIndex;
		chainCount = ImgInfo.bChainFiles ? FileCountOnly(CurrentFileIndex) : 1;
		if (ImgInfo.repeatDelay && (nRepeatsLeft > 1) || chainRepeatCount >= 1) {
			FastLED.clear(true);
			// start timer
			nTimerSeconds = ImgInfo.repeatDelay;
			while (nTimerSeconds > 0 && !CheckCancel()) {
				line = "Repeat Delay: " + String(nTimerSeconds / 10) + "." + String(nTimerSeconds % 10);
				DisplayLine(2, line, SystemInfo.menuTextColor);
				line = "";
				delay(100);
				--nTimerSeconds;
			}
		}
	}
	if (ImgInfo.bChainFiles)
		CurrentFileIndex = lastFileIndex;
	FastLED.clear(true);
	tft.fillScreen(TFT_BLACK);
	bIsRunning = false;
	if (!bRunningMacro)
		DisplayCurrentFile();
	if (bRecordingMacro) {
		// write the time for this macro into the file
		time_t now = time(NULL);
		recordingTotalTime += difftime(now, recordingTimeStart);
	}
	// clear buttons
	CRotaryDialButton::clear();
}

void SendFile(String Filename) {
	// see if there is an associated config file
	String cfFile = MakeMIWFilename(Filename, true);
	SettingsSaveRestore(true, 0);
	ProcessConfigFile(cfFile);
	String fn = currentFolder + Filename;
	dataFile = SD.open(fn);
	// if the file is available send it to the LED's
	if (dataFile.available()) {
		for (int cnt = 0; cnt < (ImgInfo.bMirrorPlayImage ? 2 : 1); ++cnt) {
			ReadAndDisplayFile(cnt == 0);
			ImgInfo.bReverseImage = !ImgInfo.bReverseImage; // note this will be restored by SettingsSaveRestore
			dataFile.seek(0);
			FastLED.clear(true);
			int wait = ImgInfo.nMirrorDelay;
			while (wait-- > 0) {
				delay(100);
			}
			if (CheckCancel())
				break;
		}
		dataFile.close();
	}
	else {
		WriteMessage("open fail: " + fn, true, 5000);
		return;
	}
	ShowProgressBar(100);
	SettingsSaveRestore(false, 0);
}

// some useful BMP constants
#define MYBMP_BF_TYPE           0x4D42	// "BM"
#define MYBMP_BI_RGB            0L
//#define MYBMP_BI_RLE8           1L
//#define MYBMP_BI_RLE4           2L
//#define MYBMP_BI_BITFIELDS      3L

void IRAM_ATTR ReadAndDisplayFile(bool doingFirstHalf) {
	static int totalSeconds;
	if (doingFirstHalf)
		totalSeconds = -1;

	// clear the file cache buffer
	readByte(true);
	uint16_t bmpType = readInt();
	uint32_t bmpSize = readLong();
	uint16_t bmpReserved1 = readInt();
	uint16_t bmpReserved2 = readInt();
	uint32_t bmpOffBits = readLong();
	//Serial.println("\nBMPtype: " + String(bmpType) + " offset: " + String(bmpOffBits));

	/* Check file header */
	if (bmpType != MYBMP_BF_TYPE) {
		WriteMessage(String("Invalid BMP:\n") + currentFolder + FileNames[CurrentFileIndex], true);
		return;
	}

	/* Read info header */
	uint32_t imgSize = readLong();
	uint32_t imgWidth = readLong();
	uint32_t imgHeight = readLong();
	uint16_t imgPlanes = readInt();
	uint16_t imgBitCount = readInt();
	uint32_t imgCompression = readLong();
	uint32_t imgSizeImage = readLong();
	uint32_t imgXPelsPerMeter = readLong();
	uint32_t imgYPelsPerMeter = readLong();
	uint32_t imgClrUsed = readLong();
	uint32_t imgClrImportant = readLong();

	//Serial.println("imgSize: " + String(imgSize));
	//Serial.println("imgWidth: " + String(imgWidth));
	//Serial.println("imgHeight: " + String(imgHeight));
	//Serial.println("imgPlanes: " + String(imgPlanes));
	//Serial.println("imgBitCount: " + String(imgBitCount));
	//Serial.println("imgCompression: " + String(imgCompression));
	//Serial.println("imgSizeImage: " + String(imgSizeImage));
	/* Check info header */
	if (imgWidth <= 0 || imgHeight <= 0 || imgPlanes != 1 ||
		imgBitCount != 24 || imgCompression != MYBMP_BI_RGB || imgSizeImage == 0)
	{
		WriteMessage(String("Unsupported, must be 24bpp:\n") + currentFolder + FileNames[CurrentFileIndex], true);
		return;
	}
	int displayWidth = imgWidth;
	if (imgWidth > LedInfo.nTotalLeds) {
		displayWidth = LedInfo.nTotalLeds;           //only display the number of led's we have
	}

	/* compute the line length */
	uint32_t lineLength = imgWidth * 3;
	// fix for padding to 4 byte words
	if ((lineLength % 4) != 0)
		lineLength = (lineLength / 4 + 1) * 4;

	// Note:  
	// The x,r,b,g sequence below might need to be changed if your strip is displaying
	// incorrect colors.  Some strips use an x,r,b,g sequence and some use x,r,g,b
	// Change the order if needed to make the colors correct.
	// init the fade settings in SetPixel
	SetPixel(0, TFT_BLACK, -1, (int)imgHeight);
	long secondsLeft = 0, lastSeconds = 0;
	char num[50];
	int percent;
	unsigned minLoopTime = 0; // the minimum time it takes to process a line
	bool bLoopTimed = false;
	if (SystemInfo.bShowDuringBmpFile) {
		ShowLeds(1);
	}
	// also remember that height and width are effectively swapped since we rotated the BMP image CCW for ease of reading and displaying here
	for (int y = ImgInfo.bReverseImage ? imgHeight - 1 : 0; ImgInfo.bReverseImage ? y >= 0 : y < imgHeight; ImgInfo.bReverseImage ? --y : ++y) {
		// approximate time left
		if (ImgInfo.bReverseImage)
			secondsLeft = ((long)y * (ImgInfo.nFrameHold + minLoopTime) / 1000L) + 1;
		else
			secondsLeft = ((long)(imgHeight - y) * (ImgInfo.nFrameHold + minLoopTime) / 1000L) + 1;
		// mark the time for timing the loop
		if (!bLoopTimed) {
			minLoopTime = millis();
		}
		if (ImgInfo.bMirrorPlayImage) {
			if (totalSeconds == -1)
				totalSeconds = secondsLeft;
			if (doingFirstHalf) {
				secondsLeft += totalSeconds;
			}
		}
		if (secondsLeft != lastSeconds) {
			lastSeconds = secondsLeft;
			sprintf(num, "File Seconds: %d", secondsLeft);
			DisplayLine(2, num, SystemInfo.menuTextColor);
		}
		percent = map(ImgInfo.bReverseImage ? imgHeight - y : y, 0, imgHeight, 0, 100);
		if (ImgInfo.bMirrorPlayImage) {
			percent /= 2;
			if (!doingFirstHalf) {
				percent += 50;
			}
		}
		if (((percent % 5) == 0) || percent > 90) {
			ShowProgressBar(percent);
		}
		int bufpos = 0;
		CRGB pixel;
		FileSeekBuf((uint32_t)bmpOffBits + (y * lineLength));
		//uint32_t offset = (bmpOffBits + (y * lineLength));
		//dataFile.seekSet(offset);
		for (int x = displayWidth - 1; x >= 0; --x) {
			// this reads three bytes
			pixel = getRGBwithGamma();
			// see if we want this one
			if (ImgInfo.bScaleHeight && (x * displayWidth) % imgWidth) {
				continue;
			}
			SetPixel(x, pixel, y);
		}
		// see how long it took to get here
		if (!bLoopTimed) {
			minLoopTime = millis() - minLoopTime;
			bLoopTimed = true;
			// if fixed time then we need to calculate the framehold value
			if (ImgInfo.bFixedTime) {
				// divide the time by the number of frames
				ImgInfo.nFrameHold = 1000 * ImgInfo.nFixedImageTime / imgHeight;
				ImgInfo.nFrameHold -= minLoopTime;
				ImgInfo.nFrameHold = max(ImgInfo.nFrameHold, 0);
			}
		}
		// wait for timer to expire before we show the next frame
		while (bStripWaiting) {
			delayMicroseconds(100);
			// we should maybe check the cancel key here to handle slow frame rates?
		}
		// now show the lights
		FastLED.show();
		if (SystemInfo.bShowDuringBmpFile) {
			ShowLeds(0);
		}
		// set a timer while we go ahead and load the next frame
		bStripWaiting = true;
		esp_timer_start_once(oneshot_LED_timer, static_cast<uint64_t>(ImgInfo.nFrameHold) * 1000);
		// check keys
		if (CheckCancel())
			break;
		if (ImgInfo.bManualFrameAdvance) {
			// check if frame advance button requested
			if (ImgInfo.nFramePulseCount) {
				for (int ix = ImgInfo.nFramePulseCount; ix; --ix) {
					// wait for press
					while (digitalRead(FRAMEBUTTON)) {
						if (CheckCancel())
							break;
						delay(10);
					}
					// wait for release
					while (!digitalRead(FRAMEBUTTON)) {
						if (CheckCancel())
							break;
						delay(10);
					}
				}
			}
			else {
				// by button click or rotate
				int btn;
				for (;;) {
					btn = ReadButton();
					if (btn == BTN_NONE)
						continue;
					else if (btn == BTN_LONG)
						CRotaryDialButton::pushButton(BTN_LONG);
					else if (btn == BTN_LEFT) {
						// backup a line, use 2 because the for loop does one when we're done here
						if (ImgInfo.bReverseImage) {
							y += 2;
							if (y > imgHeight)
								y = imgHeight;
						}
						else {
							y -= 2;
							if (y < -1)
								y = -1;
						}
						break;
					}
					else
						break;
					if (CheckCancel())
						break;
					delay(10);
				}
			}
		}
		if (bCancelRun)
			break;
	}
	// all done
	readByte(true);
	if (SystemInfo.bShowDuringBmpFile) {
		ShowLeds(2);
	}
}

// put the current file on the display
// Note that menu is not used, it is called with NULL sometimes
void ShowBmp(MenuItem*)
{
	if (SystemInfo.bShowBuiltInTests) {
		if (BuiltInFiles[CurrentFileIndex].function) {
			ShowLeds(1);    // get ready for preview
			(*BuiltInFiles[CurrentFileIndex].function)();
			ShowLeds(2);    // go back to normal
		}
		return;
	}
	String fn = currentFolder + FileNames[CurrentFileIndex];
	// make sure this is a bmp file, if not just quietly go away
	String tmp = fn.substring(fn.length() - 3);
	tmp.toLowerCase();
	if (tmp.compareTo("bmp")) {
		return;
	}
	bool bSawButton0 = !digitalRead(0);
	uint16_t* scrBuf;
	scrBuf = (uint16_t*)calloc(240 * 135, sizeof(uint16_t));
	if (scrBuf == NULL) {
		WriteMessage("Not enough memory", true, 5000);
		return;
	}
	bool bOldGamma = LedInfo.bGammaCorrection;
	LedInfo.bGammaCorrection = false;
	dataFile = SD.open(fn);
	// if the file is available send it to the LED's
	if (!dataFile.available()) {
		free(scrBuf);
		WriteMessage("failed to open: " + currentFolder + FileNames[CurrentFileIndex], true);
		return;
	}
	tft.fillScreen(TFT_BLACK);
	// clear the file cache buffer
	readByte(true);
	uint16_t bmpType = readInt();
	uint32_t bmpSize = readLong();
	uint16_t bmpReserved1 = readInt();
	uint16_t bmpReserved2 = readInt();
	uint32_t bmpOffBits = readLong();

	/* Check file header */
	if (bmpType != MYBMP_BF_TYPE) {
		free(scrBuf);
		WriteMessage(String("Invalid BMP:\n") + currentFolder + FileNames[CurrentFileIndex], true);
		return;
	}

	/* Read info header */
	uint32_t imgSize = readLong();
	uint32_t imgWidth = readLong();
	uint32_t imgHeight = readLong();
	uint16_t imgPlanes = readInt();
	uint16_t imgBitCount = readInt();
	uint32_t imgCompression = readLong();
	uint32_t imgSizeImage = readLong();
	uint32_t imgXPelsPerMeter = readLong();
	uint32_t imgYPelsPerMeter = readLong();
	uint32_t imgClrUsed = readLong();
	uint32_t imgClrImportant = readLong();

	/* Check info header */
	if (imgWidth <= 0 || imgHeight <= 0 || imgPlanes != 1 ||
		imgBitCount != 24 || imgCompression != MYBMP_BI_RGB || imgSizeImage == 0)
	{
		free(scrBuf);
		WriteMessage(String("Unsupported, must be 24bpp:\n") + currentFolder + FileNames[CurrentFileIndex], true);
		return;
	}
	bool bHalfSize = false;
	int displayWidth = imgWidth;
	if (imgWidth > LedInfo.nTotalLeds) {
		displayWidth = LedInfo.nTotalLeds;           //only display the number of led's we have
	}
	// see if this is too big for the TFT
	if (imgWidth > 144) {
		bHalfSize = true;
	}

	/* compute the line length */
	uint32_t lineLength = imgWidth * 3;
	// fix for padding to 4 byte words
	if ((lineLength % 4) != 0)
		lineLength = (lineLength / 4 + 1) * 4;
	bool done = false;
	bool redraw = true;
	bool allowScroll = imgHeight > 240;
	// offset for showing the image
	int imgOffset = 0;
	int oldImgOffset;
	bool bShowingSize = false;
	while (!done) {
		if (redraw) {
			// loop through the image, y is the image width, and x is the image height
			for (int col = 0; col < (imgHeight > 240 ? 240 : imgHeight); ++col) {
				int bufpos = 0;
				CRGB pixel;
				// get to start of pixel data for this column
				FileSeekBuf((uint32_t)bmpOffBits + (((col * (bHalfSize ? 2 : 1)) + imgOffset) * lineLength));
				for (int x = displayWidth - 1; x >= 0; --x) {
					// this reads three bytes
					pixel = getRGBwithGamma();
					if (bHalfSize)
						pixel = getRGBwithGamma();
					// add to the display memory
					int row = x - 5;
					if (row >= 0 && row < 135) {
						uint16_t color = tft.color565(pixel.r, pixel.g, pixel.b);
						uint16_t sbcolor;
						// the memory image colors are byte swapped
						swab(&color, &sbcolor, 2);
						scrBuf[(134 - row) * 240 + col] = sbcolor;
					}
				}
			}
			oldImgOffset = imgOffset;
			// got it all, go show it
			tft.pushRect(0, 0, 240, 135, scrBuf);
			// don't draw it again until something changes
			redraw = false;
			while (ReadButton() != BTN_NONE)
				;
		}
		if (bSawButton0) {
			while (digitalRead(0) == 0)
				;
			bSawButton0 = false;
			delay(30);
		}
		switch (ReadButton()) {
		case CRotaryDialButton::BTN_RIGHT:
			if (allowScroll) {
				imgOffset -= bHalfSize ? (SystemInfo.nPreviewScrollCols * 2) : SystemInfo.nPreviewScrollCols;
				imgOffset = max(0, imgOffset);
			}
			break;
		case CRotaryDialButton::BTN_LEFT:
			if (allowScroll) {
				imgOffset += bHalfSize ? (SystemInfo.nPreviewScrollCols * 2) : SystemInfo.nPreviewScrollCols;
				imgOffset = min((int32_t)imgHeight - (bHalfSize ? 480 : 240), imgOffset);
			}
			break;
		case CRotaryDialButton::BTN_LONGPRESS:
			done = true;
			break;
		case CRotaryDialButton::BTN_CLICK:
			if (bShowingSize) {
				bShowingSize = false;
				redraw = true;
			}
			else {
				tft.fillScreen(TFT_BLACK);
				DisplayLine(0, currentFolder, SystemInfo.menuTextColor);
				DisplayLine(1, FileNames[CurrentFileIndex], SystemInfo.menuTextColor);
				float walk = (float)imgHeight / (float)imgWidth;
				DisplayLine(3, String(imgWidth) + " x " + String(imgHeight) + " pixels", SystemInfo.menuTextColor);
				DisplayLine(4, String(walk, 1) + " (" + String(walk * 3.28084, 1) + ") meters(feet)", SystemInfo.menuTextColor);
				// calculate display time
				float dspTime = ImgInfo.bFixedTime ? ImgInfo.nFixedImageTime : (imgHeight * ImgInfo.nFrameHold / 1000.0 + imgHeight * .008);
				DisplayLine(5, "About " + String((int)round(dspTime)) + " Seconds", SystemInfo.menuTextColor);
				bShowingSize = true;
				redraw = false;
			}
			break;
		}
		if (oldImgOffset != imgOffset) {
			redraw = true;
		}
		// check the 0 button
		if (digitalRead(0) == 0) {
			// debounce, don't want this seen again in the main loop
			delay(30);
			done = true;
		}
		delay(2);
	}
	// all done
	free(scrBuf);
	dataFile.close();
	readByte(true);
	LedInfo.bGammaCorrection = bOldGamma;
	tft.fillScreen(TFT_BLACK);
}

void DisplayLine(int line, String text, int16_t color, int16_t backColor)
{
	// don't show if running and displaying file on LCD
	if (!(bIsRunning && SystemInfo.bShowDuringBmpFile)) {
		int y = line * tft.fontHeight();
		tft.fillRect(0, y, tft.width(), tft.fontHeight(), backColor);
		tft.setTextColor(color, backColor);
		tft.drawString(text, 0, y);
	}
}

// active menu line is in reverse video or * at front depending on bMenuStar
void DisplayMenuLine(int line, int displine, String text)
{
	bool hilite = MenuStack.top()->index == line;
	String mline = (hilite && SystemInfo.bMenuStar ? "*" : " ") + text;
	if (displine < MENU_LINES) {
		if (SystemInfo.bMenuStar) {
			DisplayLine(displine, mline, SystemInfo.menuTextColor, TFT_BLACK);
		}
		else {
			DisplayLine(displine, mline, hilite ? TFT_BLACK : SystemInfo.menuTextColor, hilite ? SystemInfo.menuTextColor : TFT_BLACK);
		}
	}
}

uint32_t IRAM_ATTR readLong() {
	uint32_t retValue;
	byte incomingbyte;

	incomingbyte = readByte(false);
	retValue = (uint32_t)((byte)incomingbyte);

	incomingbyte = readByte(false);
	retValue += (uint32_t)((byte)incomingbyte) << 8;

	incomingbyte = readByte(false);
	retValue += (uint32_t)((byte)incomingbyte) << 16;

	incomingbyte = readByte(false);
	retValue += (uint32_t)((byte)incomingbyte) << 24;

	return retValue;
}

uint16_t IRAM_ATTR readInt() {
	byte incomingbyte;
	uint16_t retValue = 0;

	incomingbyte = readByte(false);
	retValue += (uint16_t)((byte)incomingbyte);

	incomingbyte = readByte(false);
	retValue += (uint16_t)((byte)incomingbyte) << 8;

	return retValue;
}
byte filebuf[512];
int fileindex = 0;
int filebufsize = 0;
uint32_t filePosition = 0;

int IRAM_ATTR readByte(bool clear) {
	//int retbyte = -1;
	if (clear) {
		filebufsize = 0;
		fileindex = 0;
		return 0;
	}
	// TODO: this needs to align with 512 byte boundaries, maybe
	if (filebufsize == 0 || fileindex >= sizeof(filebuf)) {
		filePosition = dataFile.position();
		//// if not on 512 boundary yet, just return a byte
		//if ((filePosition % 512) && filebufsize == 0) {
		//    //Serial.println("not on 512");
		//    return dataFile.read();
		//}
		// read a block
//        Serial.println("block read");
		do {
			filebufsize = dataFile.read(filebuf, sizeof(filebuf));
		} while (filebufsize < 0);
		fileindex = 0;
	}
	return filebuf[fileindex++];
	//while (retbyte < 0) 
	//    retbyte = dataFile.read();
	//return retbyte;
}


// make sure we are the right place
void IRAM_ATTR FileSeekBuf(uint32_t place)
{
	if (place < filePosition || place >= filePosition + filebufsize) {
		// we need to read some more
		filebufsize = 0;
		dataFile.seek(place);
	}
}

// count the actual files, at a given starting point
int FileCountOnly(int start)
{
	int count = 0;
	// ignore folders, at the end
	for (int files = start; files < FileNames.size(); ++files) {
		if (!IsFolder(files))
			++count;
	}
	return count;
}

// return true if current file is folder
bool IsFolder(int index)
{
	return FileNames[index][0] == NEXT_FOLDER_CHAR
		|| FileNames[index][0] == PREVIOUS_FOLDER_CHAR;
}

// show the current file
void DisplayCurrentFile(bool path)
{
	//String name = FileNames[CurrentFileIndex];
	//String upper = name;
	//upper.toUpperCase();
 //   if (upper.endsWith(".BMP"))
 //       name = name.substring(0, name.length() - 4);
	//tft.setTextColor(TFT_BLACK, menuTextColor);
	if (SystemInfo.bShowBuiltInTests) {
		if (SystemInfo.bHiLiteCurrentFile) {
			DisplayLine(0, FileNames[CurrentFileIndex], TFT_BLACK, SystemInfo.menuTextColor);
		}
		else {
			DisplayLine(0, FileNames[CurrentFileIndex], SystemInfo.menuTextColor, TFT_BLACK);
		}
	}
	else {
		if (bSdCardValid) {
			if (SystemInfo.bHiLiteCurrentFile) {
				DisplayLine(0, ((path && SystemInfo.bShowFolder) ? currentFolder : "") + FileNames[CurrentFileIndex] + (ImgInfo.bMirrorPlayImage ? "><" : ""), TFT_BLACK, SystemInfo.menuTextColor);
			}
			else {
				DisplayLine(0, ((path && SystemInfo.bShowFolder) ? currentFolder : "") + FileNames[CurrentFileIndex] + (ImgInfo.bMirrorPlayImage ? "><" : ""), SystemInfo.menuTextColor, TFT_BLACK);
			}
		}
		else {
			WriteMessage("No SD Card or Files", true);
		}
	}
	if (!bIsRunning && SystemInfo.bShowNextFiles) {
		for (int ix = 1; ix < MENU_LINES; ++ix) {
			if (ix + CurrentFileIndex >= FileNames.size()) {
				DisplayLine(ix, "", SystemInfo.menuTextColor);
			}
			else {
				DisplayLine(ix, "   " + FileNames[CurrentFileIndex + ix], SystemInfo.menuTextColor);
			}
		}
	}
	tft.setTextColor(SystemInfo.menuTextColor);
	// for debugging keypresses
	//DisplayLine(3, String(nButtonDowns) + " " + nButtonUps);
}

void ShowProgressBar(int percent)
{
	//if (SystemInfo.bShowProgress && !(bIsRunning && SystemInfo.bShowDuringBmpFile)) {
	if (SystemInfo.bShowProgress) {
		static int lastpercent = 0;
		if (lastpercent && (lastpercent == percent))
			return;
		int x = tft.width() - 1;
		int y = SystemInfo.bShowDuringBmpFile ? 0 : (tft.fontHeight() + 4);
		int h = SystemInfo.bShowDuringBmpFile ? 4 : 8;
		if (percent == 0) {
			tft.fillRect(0, y, x, h, TFT_BLACK);
		}
		DrawProgressBar(0, y, x, h, percent, !SystemInfo.bShowDuringBmpFile);
		lastpercent = percent;
	}
}

// display message on first line
void WriteMessage(String txt, bool error, int wait)
{
	tft.fillScreen(TFT_BLACK);
	if (error) {
		txt = "**" + txt + "**";
		tft.setTextColor(TFT_RED);
	}
	else {
		tft.setTextColor(SystemInfo.menuTextColor);
	}
	tft.setCursor(0, tft.fontHeight());
	tft.setTextWrap(true);
	tft.print(txt);
	delay(wait);
	tft.setTextColor(TFT_WHITE);
}

// create the associated MIW name
String MakeMIWFilename(String filename, bool addext)
{
	String cfFile = filename;
	cfFile = cfFile.substring(0, cfFile.lastIndexOf('.'));
	if (addext)
		cfFile += String(".MIW");
	return cfFile;
}

// look for the file in the list
// return -1 if not found
int LookUpFile(String name)
{
	int ix = 0;
	for (auto &nm : FileNames) {
		if (name.equalsIgnoreCase(nm)) {
			return ix;
		}
		++ix;
	}
	return -1;
}

// process the lines in the config file
bool ProcessConfigFile(String filename)
{
	bool retval = true;
	String filepath = ((bRunningMacro || bRecordingMacro) ? String("/") : currentFolder) + filename;
#if USE_STANDARD_SD
	SDFile rdfile;
#else
	FsFile rdfile;
#endif
	rdfile = SD.open(filepath);
	if (rdfile.available()) {
		String line, command, args;
		while (line = rdfile.readStringUntil('\n'), line.length()) {
			if (CheckCancel())
				break;
			// read the lines and do what they say
			int ix = line.indexOf('=', 0);
			if (ix > 0) {
				command = line.substring(0, ix);
				command.trim();
				command.toUpperCase();
				args = line.substring(ix + 1);
				args.trim();
				// loop through the var list looking for a match
				for (int which = 0; which < sizeof(SettingsVarList) / sizeof(*SettingsVarList); ++which) {
					if (command.compareTo(SettingsVarList[which].name) == 0) {
						switch (SettingsVarList[which].type) {
						case vtInt:
						{
							int val = args.toInt();
							int min = SettingsVarList[which].min;
							int max = SettingsVarList[which].max;
							if (min != max) {
								val = constrain(val, min, max);
							}
							*(int*)(SettingsVarList[which].address) = val;
						}
						break;
						case vtBool:
							args.toUpperCase();
							*(bool*)(SettingsVarList[which].address) = args[0] == 'T';
							break;
						case vtBuiltIn:
						{
							bool bLastBuiltIn = SystemInfo.bShowBuiltInTests;
							args.toUpperCase();
							bool value = args[0] == 'T';
							if (value != bLastBuiltIn) {
								ToggleFilesBuiltin(NULL);
							}
						}
						break;
						case vtShowFile:
						{
							// get the folder and set it first
							String folder;
							String name;
							int ix = args.lastIndexOf('/');
							folder = args.substring(0, ix + 1);
							name = args.substring(ix + 1);
							int oldFileIndex = CurrentFileIndex;
							// save the old folder if necessary
							String oldFolder;
							if (!SystemInfo.bShowBuiltInTests && !currentFolder.equalsIgnoreCase(folder)) {
								oldFolder = currentFolder;
								currentFolder = folder;
								GetFileNamesFromSD(folder);
							}
							// search for the file in the list
							int which = LookUpFile(name);
							if (which >= 0) {
								CurrentFileIndex = which;
								// call the process routine
								strcpy(FileToShow, name.c_str());
								tft.fillScreen(TFT_BLACK);
								ProcessFileOrTest();
							}
							if (oldFolder.length()) {
								currentFolder = oldFolder;
								GetFileNamesFromSD(currentFolder);
							}
							CurrentFileIndex = oldFileIndex;
						}
						break;
						case vtRGB:
						{
							// handle the RBG colors
							CRGB* cp = (CRGB*)(SettingsVarList[which].address);
							cp->r = args.toInt();
							args = args.substring(args.indexOf(',') + 1);
							cp->g = args.toInt();
							args = args.substring(args.indexOf(',') + 1);
							cp->b = args.toInt();
						}
						break;
						default:
							break;
						}
						// we found it, so carry on
						break;
					}
				}
			}
		}
		rdfile.close();
	}
	else
		retval = false;
	return retval;
}

// read the files from the card or list the built-ins
// look for start.MIW, and process it, but don't add it to the list
bool GetFileNamesFromSD(String dir) {
	// start over
	// first empty the current file names
	FileNames.clear();
	if (nBootCount == 0)
		CurrentFileIndex = 0;
	if (SystemInfo.bShowBuiltInTests) {
		for (int ix = 0; ix < (sizeof(BuiltInFiles) / sizeof(*BuiltInFiles)); ++ix) {
			FileNames.push_back(String(BuiltInFiles[ix].text));
		}
	}
	else {
		String startfile;
		if (dir.length() > 1)
			dir = dir.substring(0, dir.length() - 1);
#if USE_STANDARD_SD
		File root = SD.open(dir);
		File file;
#else
		FsFile root = SD.open(dir, O_RDONLY);
		FsFile file;
#endif
		String CurrentFilename = "";
		if (!root) {
			//Serial.println("Failed to open directory: " + dir);
			//Serial.println("error: " + String(root.getError()));
			//SD.errorPrint("fail");
			return false;
		}
		if (!root.isDirectory()) {
			//Serial.println("Not a directory: " + dir);
			return false;
		}

		file = root.openNextFile();
		if (dir != "/") {
			// add an arrow to go back
			String sdir = currentFolder.substring(0, currentFolder.length() - 1);
			sdir = sdir.substring(0, sdir.lastIndexOf("/"));
			if (sdir.length() == 0)
				sdir = "/";
			FileNames.push_back(String(PREVIOUS_FOLDER_CHAR));
		}
		while (file) {
#if USE_STANDARD_SD
			CurrentFilename = file.name();
#else
			char fname[100];
			file.getName(fname, sizeof(fname));
			CurrentFilename = fname;
#endif
			// strip path
			CurrentFilename = CurrentFilename.substring(CurrentFilename.lastIndexOf('/') + 1);
			//Serial.println("name: " + CurrentFilename);
			if (CurrentFilename != "System Volume Information") {
				if (file.isDirectory()) {
					FileNames.push_back(String(NEXT_FOLDER_CHAR) + CurrentFilename);
				}
				else {
					String uppername = CurrentFilename;
					uppername.toUpperCase();
					if (uppername.endsWith(".BMP")) { //find files with our extension only
						//Serial.println("name: " + CurrentFilename);
						FileNames.push_back(CurrentFilename);
					}
					else if (uppername == "START.MIW") {
						startfile = CurrentFilename;
					}
				}
			}
			file.close();
			file = root.openNextFile();
		}
		root.close();
		std::sort(FileNames.begin(), FileNames.end(), CompareNames);
		// see if we need to process the auto start file
		if (startfile.length())
			ProcessConfigFile(startfile);
	}
	return true;
}

// compare strings for sort ignoring case
bool CompareNames(const String& a, const String& b)
{
	String a1 = a, b1 = b;
	a1.toUpperCase();
	b1.toUpperCase();
	// force folders to sort last
	if (a1[0] == NEXT_FOLDER_CHAR)
		a1[0] = '0x7e';
	if (b1[0] == NEXT_FOLDER_CHAR)
		b1[0] = '0x7e';
	// force previous folder to sort first
	if (a1[0] == PREVIOUS_FOLDER_CHAR)
		a1[0] = '0' - 1;
	if (b1[0] == PREVIOUS_FOLDER_CHAR)
		b1[0] = '0' - 1;
	return a1.compareTo(b1) < 0;
}

// save and restore important settings, two sets are available
// 0 is used by file display, and 1 is used when running macros
bool SettingsSaveRestore(bool save, int set)
{
	static void* memptr[2] = { NULL, NULL };
	if (save) {
		// get some memory and save the values
		if (memptr[set])
			free(memptr[set]);
		// calculate how many bytes we need
		size_t neededBytes = 0;
		for (int ix = 0; ix < (sizeof(saveValueList) / sizeof(*saveValueList)); ++ix) {
			neededBytes += saveValueList[ix].size;
		}
		memptr[set] = malloc(neededBytes);
		if (!memptr[set]) {
			return false;
		}
	}
	void* blockptr = memptr[set];
	if (memptr[set] == NULL) {
		return false;
	}
	for (int ix = 0; ix < (sizeof(saveValueList) / sizeof(*saveValueList)); ++ix) {
		if (save) {
			memcpy(blockptr, saveValueList[ix].val, saveValueList[ix].size);
		}
		else {
			memcpy(saveValueList[ix].val, blockptr, saveValueList[ix].size);
		}
		blockptr = (void*)((byte*)blockptr + saveValueList[ix].size);
	}
	if (!save) {
		// if it was saved, restore it and free the memory
		if (memptr[set]) {
			free(memptr[set]);
			memptr[set] = NULL;
		}
	}
	return true;
}

void EraseStartFile(MenuItem* menu)
{
	WriteOrDeleteConfigFile("", true, true);
}

void SaveStartFile(MenuItem* menu)
{
	WriteOrDeleteConfigFile("", false, true);
}

void EraseAssociatedFile(MenuItem* menu)
{
	WriteOrDeleteConfigFile(FileNames[CurrentFileIndex].c_str(), true, false);
}

void SaveAssociatedFile(MenuItem* menu)
{
	WriteOrDeleteConfigFile(FileNames[CurrentFileIndex].c_str(), false, false);
}

void LoadAssociatedFile(MenuItem* menu)
{
	String name = FileNames[CurrentFileIndex];
	name = MakeMIWFilename(name, true);
	if (ProcessConfigFile(name)) {
		WriteMessage(String("Processed:\n") + name);
	}
	else {
		WriteMessage(String("Failed reading:\n") + name, true);
	}
}

void LoadStartFile(MenuItem* menu)
{
	String name = "START.MIW";
	if (ProcessConfigFile(name)) {
		WriteMessage(String("Processed:\n") + name);
	}
	else {
		WriteMessage("Failed reading:\n" + name, true);
	}
}

// create the config file, or remove it
// startfile true makes it use the start.MIW file, else it handles the associated name file
bool WriteOrDeleteConfigFile(String filename, bool remove, bool startfile)
{
	bool retval = true;
	String filepath;
	if (startfile) {
		filepath = currentFolder + String("START.MIW");
	}
	else {
		filepath = ((bRecordingMacro || bRunningMacro) ? String("/") : currentFolder) + MakeMIWFilename(filename, true);
	}
	if (remove) {
		if (!SD.exists(filepath.c_str()))
			WriteMessage(String("Not Found:\n") + filepath);
		else if (SD.remove(filepath.c_str())) {
			WriteMessage(String("Erased:\n") + filepath);
		}
		else {
			WriteMessage(String("Failed to erase:\n") + filepath, true);
		}
	}
	else {
		String line;
#if USE_STANDARD_SD
		File file = SD.open(filepath.c_str(), bRecordingMacro ? FILE_APPEND : FILE_WRITE);
#else
		FsFile file = SD.open(filepath.c_str(), bRecordingMacro ? (O_APPEND | O_WRITE | O_CREAT) : (O_WRITE | O_TRUNC | O_CREAT));
#endif
		if (file) {
			// loop through the var list
			for (int ix = 0; ix < sizeof(SettingsVarList) / sizeof(*SettingsVarList); ++ix) {
				switch (SettingsVarList[ix].type) {
				case vtBuiltIn:
					line = String(SettingsVarList[ix].name) + "=" + String(*(bool*)(SettingsVarList[ix].address) ? "TRUE" : "FALSE");
					break;
				case vtShowFile:
					if (*(char*)(SettingsVarList[ix].address)) {
						line = String(SettingsVarList[ix].name) + "=" + (SystemInfo.bShowBuiltInTests ? "" : currentFolder) + String((char*)(SettingsVarList[ix].address));
					}
					break;
				case vtInt:
					line = String(SettingsVarList[ix].name) + "=" + String(*(int*)(SettingsVarList[ix].address));
					break;
				case vtBool:
					line = String(SettingsVarList[ix].name) + "=" + String(*(bool*)(SettingsVarList[ix].address) ? "TRUE" : "FALSE");
					break;
				case vtRGB:
				{
					// handle the RBG colors
					CRGB* cp = (CRGB*)(SettingsVarList[ix].address);
					line = String(SettingsVarList[ix].name) + "=" + String(cp->r) + "," + String(cp->g) + "," + String(cp->b);
				}
				break;
				default:
					line = "";
					break;
				}
				if (line.length())
					file.println(line);
			}
			file.close();
			WriteMessage(String("Saved:\n") + filepath);
		}
		else {
			retval = false;
			WriteMessage(String("Failed to write:\n") + filepath, true);
		}
	}
	return retval;
}

// save the eeprom settings
void SaveEepromSettings(MenuItem* menu)
{
	SaveLoadSettings(true);
}

// load eeprom settings
void LoadEepromSettings(MenuItem* menu)
{
	SaveLoadSettings(false);
}

// save the macro with the current settings
void SaveMacro(MenuItem* menu)
{
	bRecordingMacro = true;
	WriteOrDeleteConfigFile(String(ImgInfo.nCurrentMacro), false, false);
	bRecordingMacro = false;
}

// saves and restores settings
void RunMacro(MenuItem* menu)
{
	bCancelMacro = false;
	for (nMacroRepeatsLeft = ImgInfo.nRepeatCountMacro; nMacroRepeatsLeft; --nMacroRepeatsLeft) {
		MacroLoadRun(menu, true);
		if (bCancelMacro) {
			break;
		}
		tft.fillScreen(TFT_BLACK);
		for (int wait = ImgInfo.nRepeatWaitMacro; nMacroRepeatsLeft > 1 && wait; --wait) {
			if (CheckCancel()) {
				nMacroRepeatsLeft = 0;
				break;
			}
			DisplayLine(5, "#" + String(ImgInfo.nCurrentMacro) + String(" Wait: ") + String(wait / 10) + "." + String(wait % 10) + " Repeat: " + String(nMacroRepeatsLeft - 1), SystemInfo.menuTextColor);
			delay(100);
		}
	}
	bCancelMacro = false;
}

// like run, but doesn't restore settings
void LoadMacro(MenuItem* menu)
{
	MacroLoadRun(menu, false);
}

void MacroLoadRun(MenuItem* menu, bool save)
{
	bool oldShowBuiltins;
	if (save) {
		oldShowBuiltins = SystemInfo.bShowBuiltInTests;
		SettingsSaveRestore(true, 1);
	}
	bRunningMacro = true;
	bRecordingMacro = false;
	String line = String(ImgInfo.nCurrentMacro) + ".miw";
	if (!ProcessConfigFile(line)) {
		line += " not found";
		WriteMessage(line, true);
	}
	bRunningMacro = false;
	if (save) {
		// need to handle if the builtins was changed
		if (oldShowBuiltins != SystemInfo.bShowBuiltInTests) {
			ToggleFilesBuiltin(NULL);
		}
		SettingsSaveRestore(false, 1);
	}
}

void DeleteMacro(MenuItem* menu)
{
	WriteOrDeleteConfigFile(String(ImgInfo.nCurrentMacro), true, false);
}

// show some LED's with and without white balance adjust
void ShowWhiteBalance(MenuItem* menu)
{
	for (int ix = 0; ix < 32; ++ix) {
		SetPixel(ix, CRGB(255, 255, 255));
	}
	FastLED.setTemperature(CRGB(255, 255, 255));
	FastLED.show();
	delay(2000);
	FastLED.clear(true);
	delay(50);
	FastLED.setTemperature(CRGB(LedInfo.whiteBalance.r, LedInfo.whiteBalance.g, LedInfo.whiteBalance.b));
	FastLED.show();
	delay(3000);
	FastLED.clear(true);
}

// reverse the strip index order for the lower strip, the upper strip is normal
// also check to make sure it isn't out of range
int AdjustStripIndex(int ix)
{
	int ledCount = LedInfo.bSecondController ? LedInfo.nTotalLeds / 2 : LedInfo.nTotalLeds;
	switch (LedInfo.stripsMode) {
	case STRIPS_MIDDLE_WIRED:	// bottom reversed, top normal, both wired in the middle
		if (ix < ledCount) {
			ix = (ledCount - 1 - ix);
		}
		break;
	case STRIPS_CHAINED:	// bottom and top normal, chained, so nothing to do
		break;
	case STRIPS_OUTSIDE_WIRED:	// top reversed, bottom normal, no connection in the middle
		if (ix >= ledCount) {
			ix = (LedInfo.nTotalLeds - 1 - ix) + ledCount;
		}
		break;
	}
	// make sure it isn't too big or too small
	ix = constrain(ix, 0, LedInfo.nTotalLeds - 1);
	return ix;
}

// write a pixel to the correct location
// pixel doubling is handled here
// e.g. pixel 0 will be 0 and 1, 1 will be 2 and 3, etc
// if upside down n will be n and n-1, n-1 will be n-1 and n-2
// column = -1 to init fade in/out values
void IRAM_ATTR SetPixel(int ix, CRGB pixel, int column, int totalColumns)
{
	static int fadeStep;
	static int fadeColumns;
	static int lastColumn;
	static int maxColumn;
	static int fade;
	if (ImgInfo.nFadeInOutFrames) {
		// handle fading
		if (column == -1) {
			fadeColumns = min(totalColumns / 2, ImgInfo.nFadeInOutFrames);
			maxColumn = totalColumns;
			fadeStep = 255 / fadeColumns;
			//Serial.println("fadeStep: " + String(fadeStep) + " fadeColumns: " + String(fadeColumns) + " maxColumn: " + String(maxColumn));
			lastColumn = -1;
			fade = 255;
			return;
		}
		// when the column changes check if we are in the fade areas
		if (column != lastColumn) {
			int realColumn = ImgInfo.bReverseImage ? maxColumn - 1 - column : column;
			if (realColumn <= fadeColumns) {
				// calculate the fade amount
				fade = realColumn * fadeStep;
				fade = constrain(fade, 0, 255);
				// fading up
				//Serial.println("UP col: " + String(realColumn) + " fade: " + String(fade));
			}
			else if (realColumn >= maxColumn - 1 - fadeColumns) {
				// calculate the fade amount
				fade = (maxColumn - 1 - realColumn) * fadeStep;
				fade = constrain(fade, 0, 255);
				// fading down
				//Serial.println("DOWN col: " + String(realColumn) + " fade: " + String(fade));
			}
			else
				fade = 255;
			lastColumn = column;
		}
	}
	else {
		// no fade
		fade = 255;
	}
	int ix1, ix2;
	if (ImgInfo.bUpsideDown) {
		if (ImgInfo.bDoublePixels && !SystemInfo.bShowBuiltInTests) {
			ix1 = AdjustStripIndex(LedInfo.nTotalLeds - 1 - 2 * ix);
			ix2 = AdjustStripIndex(LedInfo.nTotalLeds - 2 - 2 * ix);
		}
		else {
			ix1 = AdjustStripIndex(LedInfo.nTotalLeds - 1 - ix);
		}
	}
	else {
		if (ImgInfo.bDoublePixels && !SystemInfo.bShowBuiltInTests) {
			ix1 = AdjustStripIndex(2 * ix);
			ix2 = AdjustStripIndex(2 * ix + 1);
		}
		else {
			ix1 = AdjustStripIndex(ix);
		}
	}
	if (fade != 255) {
		pixel = pixel.nscale8_video(fade);
		//Serial.println("col: " + String(column) + " fade: " + String(fade));
	}
	leds[ix1] = pixel;
	if (ImgInfo.bDoublePixels && !SystemInfo.bShowBuiltInTests)
		leds[ix2] = pixel;
}

#define Fbattery    3700  //The default battery is 3700mv when the battery is fully charged.

float XS = 0.00225;      //The returned reading is multiplied by this XS to get the battery voltage.
uint16_t MUL = 1000;
uint16_t MMUL = 100;
// read and display the battery voltage
void ReadBattery(MenuItem* menu)
{
	//tft.clear();
	uint16_t bat = analogRead(A4);
	Serial.println("bat: " + String(bat));
	DisplayLine(0, "Battery: " + String(bat));
	delay(1000);
	//uint16_t c = analogRead(13) * XS * MUL;
	////uint16_t d  =  (analogRead(13)*XS*MUL*MMUL)/Fbattery;
	//Serial.println(analogRead(13));
	////Serial.println((String)d);
 //  // Serial.printf("%x",analogRead(13));
	//Heltec.display->drawString(0, 0, "Remaining battery still has:");
	//Heltec.display->drawString(0, 10, "VBAT:");
	//Heltec.display->drawString(35, 10, (String)c);
	//Heltec.display->drawString(60, 10, "(mV)");
	//// Heltec.display->drawString(90, 10, (String)d);
	//// Heltec.display->drawString(98, 10, ".";
	//// Heltec.display->drawString(107, 10, "%");
	//Heltec.display->display();
	//delay(2000);
	//Heltec.display->clear();
 //Battery voltage read pin changed from GPIO13 to GPI37
	////adcStart(37);
	////while (adcBusy(37))
	////	;
	////Serial.printf("Battery power in GPIO 37: ");
	////Serial.println(analogRead(37));
	////uint16_t c1 = analogRead(37) * XS * MUL;
	////adcEnd(37);
	////Serial.println("Vbat: " + String(c1));

	////delay(100);

	//adcStart(36);
	//while (adcBusy(36));
	//Serial.printf("voltage input on GPIO 36: ");
	//Serial.println(analogRead(36));
	//uint16_t c2 = analogRead(36) * 0.769 + 150;
	//adcEnd(36);
	//Serial.println("-------------");
	// uint16_t c  =  analogRead(13)*XS*MUL;
	// Serial.println(analogRead(13));
	////Heltec.display->drawString(0, 0, "Vbat = ");
	////Heltec.display->drawString(33, 0, (String)c1);
	////Heltec.display->drawString(60, 0, "(mV)");

	//Heltec.display->drawString(0, 10, "Vin   = ");
	//Heltec.display->drawString(33, 10, (String)c2);
	//Heltec.display->drawString(60, 10, "(mV)");

	// Heltec.display->drawString(0, 0, "Remaining battery still has:");
	// Heltec.display->drawString(0, 10, "VBAT:");
	// Heltec.display->drawString(35, 10, (String)c);
	// Heltec.display->drawString(60, 10, "(mV)");
	////Heltec.display->display();
	////delay(2000);
}

// grow and shrink a rainbow type pattern
#define PI_SCALE 2
#define TWO_HUNDRED_PI (628*PI_SCALE)
void RainbowPulse()
{
	int element = 0;
	int last_element = 0;
	int highest_element = 0;
	//Serial.println("second: " + String(bSecondStrip));
	//Serial.println("Len: " + String(STRIPLENGTH));
	for (int i = 0; i < TWO_HUNDRED_PI; i++) {
		element = round((LedInfo.nTotalLeds - 1) / 2 * (-cos(i / (PI_SCALE * 100.0)) + 1));
		//Serial.println("elements: " + String(element) + " " + String(last_element));
		if (element > last_element) {
			SetPixel(element, CHSV(element * BuiltinInfo.nRainbowPulseColorScale + BuiltinInfo.nRainbowPulseStartColor, BuiltinInfo.nRainbowPulseSaturation, 255));
			ShowLeds();
			//FastLED.show();
			highest_element = max(highest_element, element);
		}
		if (CheckCancel()) {
			break;
		}
		delayMicroseconds(BuiltinInfo.nRainbowPulsePause * 10);
		if (element < last_element) {
			// cleanup the highest one
			SetPixel(highest_element, CRGB::Black);
			SetPixel(element, CRGB::Black);
			ShowLeds();
			//FastLED.show();
		}
		last_element = element;
	}
}

/*
	Write a wedge in time, from the middle out
*/
void TestWedge()
{
	int midPoint = LedInfo.nTotalLeds / 2 - 1;
	for (int ix = 0; ix < LedInfo.nTotalLeds / 2; ++ix) {
		SetPixel(midPoint + ix, CRGB(BuiltinInfo.nWedgeRed, BuiltinInfo.nWedgeGreen, BuiltinInfo.nWedgeBlue));
		SetPixel(midPoint - ix, CRGB(BuiltinInfo.nWedgeRed, BuiltinInfo.nWedgeGreen, BuiltinInfo.nWedgeBlue));
		if (!BuiltinInfo.bWedgeFill) {
			if (ix > 1) {
				SetPixel(midPoint + ix - 1, CRGB::Black);
				SetPixel(midPoint - ix + 1, CRGB::Black);
			}
			else {
				SetPixel(midPoint, CRGB::Black);
			}
		}
		ShowLeds();
		//FastLED.show();
		delay(ImgInfo.nFrameHold);
		if (CheckCancel()) {
			return;
		}
	}
	FastLED.clear(true);
}

//#define NUM_LEDS 22
//#define DATA_PIN 5
//#define TWO_HUNDRED_PI 628
//#define TWO_THIRDS_PI 2.094
//
//void loop()
//{
//	int val1 = 0;
//	int val2 = 0;
//	int val3 = 0;
//	for (int i = 0; i < TWO_HUNDRED_PI; i++) {
//		val1 = round(255 / 2.0 * (sin(i / 100.0) + 1));
//		val2 = round(255 / 2.0 * (sin(i / 100.0 + TWO_THIRDS_PI) + 1));
//		val3 = round(255 / 2.0 * (sin(i / 100.0 - TWO_THIRDS_PI) + 1));
//
//		leds[7] = CHSV(0, 255, val1);
//		leds[8] = CHSV(96, 255, val2);
//		leds[9] = CHSV(160, 255, val3);
//
//		FastLED.show();
//
//		delay(1);
//	}
//}
// #########################################################################
// Fill screen with a rainbow pattern
// #########################################################################
byte red = 31;
byte green = 0;
byte blue = 0;
byte state = 0;
unsigned int colour = red << 11; // Colour order is RGB 5+6+5 bits each

void rainbow_fill()
{
	// The colours and state are not initialised so the start colour changes each time the function is called

	for (int i = 319; i >= 0; i--) {
		// Draw a vertical line 1 pixel wide in the selected colour
		tft.drawFastHLine(0, i, tft.width(), colour); // in this example tft.width() returns the pixel width of the display
		// This is a "state machine" that ramps up/down the colour brightnesses in sequence
		switch (state) {
		case 0:
			green++;
			if (green == 64) {
				green = 63;
				state = 1;
			}
			break;
		case 1:
			red--;
			if (red == 255) {
				red = 0;
				state = 2;
			}
			break;
		case 2:
			blue++;
			if (blue == 32) {
				blue = 31;
				state = 3;
			}
			break;
		case 3:
			green--;
			if (green == 255) {
				green = 0;
				state = 4;
			}
			break;
		case 4:
			red++;
			if (red == 32) {
				red = 31;
				state = 5;
			}
			break;
		case 5:
			blue--;
			if (blue == 255) {
				blue = 0;
				state = 0;
			}
			break;
		}
		colour = red << 11 | green << 5 | blue;
	}
}

// draw a progress bar
void DrawProgressBar(int x, int y, int dx, int dy, int percent, bool rect)
{
	if (rect)
		tft.drawRoundRect(x, y, dx, dy, 2, SystemInfo.menuTextColor);
	int fill = (dx - 2) * percent / 100;
	// fill the filled part
	tft.fillRect(x + 1, y + 1, fill, dy - 2, TFT_DARKGREEN);
	// blank the empty part
	tft.fillRect(x + 1 + fill, y + 1, dx - 2 - fill, dy - 2, TFT_BLACK);
}

// save/load settings
// return false if not found or wrong version
bool SaveLoadSettings(bool save, bool autoloadonly, bool ledonly, bool nodisplay)
{
	bool retvalue = true;
	Preferences prefs;
	prefs.begin(prefsName, !save);
	if (save) {
		Serial.println("saving");
		prefs.putString(prefsVersion, myVersion);
		prefs.putBool(prefsAutoload, bAutoLoadSettings);
		// save things
		if (!ledonly) {
			prefs.putBytes(prefsImgInfo, &ImgInfo, sizeof(ImgInfo));
			prefs.putBytes(prefsBuiltInInfo, &BuiltinInfo, sizeof(BuiltinInfo));
			prefs.putBytes(prefsSystemInfo, &SystemInfo, sizeof(SystemInfo));
			prefs.putInt(prefsLongPressTimer, CRotaryDialButton::m_nLongPressTimerValue);
			prefs.putInt(prefsDialSensitivity, CRotaryDialButton::m_nDialSensitivity);
			prefs.putInt(prefsDialSpeed, CRotaryDialButton::m_nDialSpeed);
			prefs.putBool(prefsDialReverse, CRotaryDialButton::m_bReverseDial);
			if (!nodisplay)
				WriteMessage("Settings Saved", false, 500);
		}
		// we always do these since they are hardware related
		prefs.putBytes(prefsLedInfo, &LedInfo, sizeof(LedInfo));
	}
	else {
		// load things
		String vsn = prefs.getString(prefsVersion, "");
		if (vsn == myVersion) {
			if (autoloadonly) {
				bAutoLoadSettings = prefs.getBool(prefsAutoload, false);
				Serial.println("getting autoload: " + String(bAutoLoadSettings));
			}
			else if (!ledonly) {
				prefs.getBytes(prefsImgInfo, &ImgInfo, sizeof(ImgInfo));
				prefs.getBytes(prefsBuiltInInfo, &BuiltinInfo, sizeof(BuiltinInfo));
				prefs.getBytes(prefsSystemInfo, &SystemInfo, sizeof(SystemInfo));
				CRotaryDialButton::m_nLongPressTimerValue = prefs.getInt(prefsLongPressTimer, 40);
				CRotaryDialButton::m_nDialSensitivity = prefs.getInt(prefsDialSensitivity, 1);
				CRotaryDialButton::m_nDialSpeed = prefs.getInt(prefsDialSpeed, 300);
				CRotaryDialButton::m_bReverseDial = prefs.getBool(prefsDialReverse, false);
				int savedFileIndex = CurrentFileIndex;
				// we don't know the folder path, so just reset the folder level
				currentFolder = "/";
				setupSDcard();
				CurrentFileIndex = savedFileIndex;
				// make sure file index isn't too big
				if (CurrentFileIndex >= FileNames.size()) {
					CurrentFileIndex = 0;
				}
				// set the brightness values since they might have changed
				SetDisplayBrightness(SystemInfo.nDisplayBrightness);
				if (!nodisplay)
					WriteMessage("Settings Loaded", false, 500);
			}
			prefs.getBytes(prefsLedInfo, &LedInfo, sizeof(LedInfo));
		}
		else {
			retvalue = false;
			if (!nodisplay)
				WriteMessage("Settings not saved yet", true, 2000);
		}
	}
	prefs.end();
	return retvalue;
}

// delete saved settings
void FactorySettings(MenuItem* menu)
{
	Preferences prefs;
	prefs.begin(prefsName);
	prefs.clear();
	prefs.end();
	ESP.restart();
}

void EraseFlash(MenuItem* menu)
{
	nvs_flash_erase(); // erase the NVS partition and...
	nvs_flash_init(); // initialize the NVS partition.
	//SaveLoadSettings(true);
}

void ReportCouldNotCreateFile(String target){
  SendHTML_Header();
  webpage += F("<h3>Could Not Create Uploaded File (write-protected?)</h3>"); 
  webpage += F("<a href='/"); webpage += target + "'>[Back]</a><br><br>";
  append_page_footer();
  SendHTML_Content();
  SendHTML_Stop();
}

FsFile UploadFile; // I would need some Help here, Martin
void handleFileUpload(){ // upload a new file to the Filing system
  HTTPUpload& uploadfile = server.upload(); // See https://github.com/esp8266/Arduino/tree/master/libraries/ESP8266WebServer/srcv
                                            // For further information on 'status' structure, there are other reasons such as a failed transfer that could be used
  if (uploadfile.status == UPLOAD_FILE_START)
  {
    String filename = uploadfile.filename;
    String filepath = String("/");
    if(!filename.startsWith("/")) filename = "/"+filename;
    Serial.print("Upload File Name: "); Serial.println(filename);
    SD.remove(filename);                         // Remove a previous version, otherwise data is appended the file again
    //UploadFile = SD.open(filename, FILE_WRITE);  // Open the file for writing in SPIFFS (create it, if doesn't exist)
	UploadFile = SD.open(filename, O_WRITE | O_CREAT);
    filename = String();
  }
  else if (uploadfile.status == UPLOAD_FILE_WRITE)
  {
    if(UploadFile) UploadFile.write(uploadfile.buf, uploadfile.currentSize); // Write the received bytes to the file
  } 
  else if (uploadfile.status == UPLOAD_FILE_END)
  {
    if(UploadFile)          // If the file was successfully created
    {                                    
      UploadFile.close();   // Close the file again
      Serial.print("Upload Size: "); Serial.println(uploadfile.totalSize);
      webpage = "";
      append_page_header();
      webpage += F("<h3>File was successfully uploaded</h3>"); 
      webpage += F("<h2>Uploaded File Name: "); webpage += uploadfile.filename+"</h2>";
      webpage += F("<h2>File Size: "); webpage += file_size(uploadfile.totalSize) + "</h2><br>"; 
      append_page_footer();
      server.send(200,"text/html",webpage);
	  // reload the file list, if not showing built-ins
	  if (!SystemInfo.bShowBuiltInTests) {
		  GetFileNamesFromSD(currentFolder);
	  }
    } 
    else
    {
      ReportCouldNotCreateFile("upload");
    }
  }
}


void append_page_header() {
  webpage  = F("<!DOCTYPE html><html>");
  webpage += F("<head>");
  webpage += F("<title>MagicImageWand</title>");
  webpage += F("<meta name='viewport' content='user-scalable=yes,initial-scale=1.0,width=device-width'>");
  webpage += F("<style>");
  webpage += F("body{max-width:98%;margin:0 auto;font-family:arial;font-size:100%;text-align:center;color:black;background-color:#888888;}");
  webpage += F("ul{list-style-type:none;margin:0.1em;padding:0;border-radius:0.17em;overflow:hidden;background-color:#EEEEEE;font-size:1em;}");
  webpage += F("li{float:left;border-radius:0.17em;border-right:0.06em solid #bbb;}last-child {border-right:none;font-size:85%}");
  webpage += F("li a{display: block;border-radius:0.17em;padding:0.44em 0.44em;text-decoration:none;font-size:65%}");
  webpage += F("li a:hover{background-color:#DDDDDD;border-radius:0.17em;font-size:85%}");
  webpage += F("section {font-size:0.88em;}");
  webpage += F("h1{color:white;border-radius:0.5em;font-size:1em;padding:0.2em 0.2em;background:#444444;}");
  webpage += F("h2{color:orange;font-size:1.0em;}");
  webpage += F("h3{font-size:0.8em;}");
  webpage += F("table{font-family:arial,sans-serif;font-size:0.9em;border-collapse:collapse;width:100%;}"); 
  webpage += F("th,td {border:0.06em solid #dddddd;text-align:left;padding:0.3em;border-bottom:0.06em solid #dddddd;}"); 
  webpage += F("tr:nth-child(odd) {background-color:#eeeeee;}");
  webpage += F(".rcorners_n {border-radius:0.2em;background:#CCCCCC;padding:0.3em 0.3em;width:100%;color:white;font-size:75%;}");
  webpage += F(".rcorners_m {border-radius:0.2em;background:#CCCCCC;padding:0.3em 0.3em;width:100%;color:white;font-size:75%;}");
  webpage += F(".rcorners_w {border-radius:0.2em;background:#CCCCCC;padding:0.3em 0.3em;width:100%;color:white;font-size:75%;}");
  webpage += F(".column{float:left;width:100%;height:100%;}");
  webpage += F(".row:after{content:'';display:table;clear:both;}");
  webpage += F("*{box-sizing:border-box;}");
  webpage += F("footer{background-color:#AAAAAA; text-align:center;padding:0.3em 0.3em;border-radius:0.375em;font-size:60%;}");
  webpage += F("button{border-radius:0.5em;background:#666666;padding:0.3em 0.3em;width:45%;color:white;font-size:100%;}");
  webpage += F(".buttons {border-radius:0.5em;background:#666666;padding:0.3em 0.3em;width:45%;color:white;font-size:80%;}");
  webpage += F(".buttonsm{border-radius:0.5em;background:#666666;padding:0.3em 0.3em;width45%; color:white;font-size:70%;}");
  webpage += F(".buttonm {border-radius:0.5em;background:#666666;padding:0.3em 0.3em;width:45%;color:white;font-size:70%;}");
  webpage += F(".buttonw {border-radius:0.5em;background:#666666;padding:0.3em 0.3em;width:45%;color:white;font-size:70%;}");
  webpage += F("a{font-size:75%;}");
  webpage += F("p{font-size:75%;}");
  webpage += F("</style></head><body><h1>MIW Server<br>"); webpage + "</h1>";
}

void append_page_footer(){
  webpage += "<ul>";
  webpage += "<li><a href='/'>Home</a></li>";
  webpage += "<li><a href='/download'>Download</a></li>"; 
  webpage += "<li><a href='/upload'>Upload</a></li>";
  webpage += "<li><a href='/settings'>Settings</a></li>";
  webpage += "</ul>";
  webpage += "<footer>MagicImageWand ";
  webpage += myVersion;
  webpage += "</footer>";
  webpage += "</body></html>";
}

void SendHTML_Header(){
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate"); 
  server.sendHeader("Pragma", "no-cache"); 
  server.sendHeader("Expires", "-1"); 
  server.setContentLength(CONTENT_LENGTH_UNKNOWN); 
  server.send(200, "text/html", ""); // Empty content inhibits Content-length header so we have to close the socket ourselves. 
  append_page_header();
  server.sendContent(webpage);
  webpage = "";
}

void SendHTML_Content(){
  server.sendContent(webpage);
  webpage = "";
}

void SendHTML_Stop(){
  server.sendContent("");
  server.client().stop(); // Stop is needed because no content length was sent
}

void HomePage(){
  SendHTML_Header();
  webpage += "<a href='/download'><button style=\"width:auto\">Download</button></a>";
  webpage += "<a href='/upload'><button style=\"width:auto\">Upload</button></a>";
  webpage += "<a href='/settings'><button style=\"width:auto\">Settings</button></a>";
  append_page_footer();
  SendHTML_Content();
  SendHTML_Stop();
}


void SelectInput(String heading1, String command, String arg_calling_name){
  SendHTML_Header();
  webpage += F("<h3>"); webpage += heading1 + "</h3>"; 
  for (String var : FileNames)
  {
	  webpage += String("<p>") + var;
  }
  webpage += F("<FORM action='/"); webpage += command + "' method='post'>";
  webpage += F("<input type='text' name='"); webpage += arg_calling_name; webpage += F("' value=''><br>");
  webpage += F("<type='submit' name='"); webpage += arg_calling_name; webpage += F("' value=''><br>");
  webpage += F("<a href='/'>[Back]</a>");
  append_page_footer();
  SendHTML_Content();
  SendHTML_Stop();
}

void File_Download(){ // This gets called twice, the first pass selects the input, the second pass then processes the command line arguments
  if (server.args() > 0 ) { // Arguments were received
    if (server.hasArg("download")) SD_file_download(server.arg(0));
  }
  else SelectInput("Enter filename to download","download","download");
}

void ReportFileNotPresent(String target){
  SendHTML_Header();
  webpage += F("<h3>File does not exist</h3>"); 
  webpage += F("<a href='/"); webpage += target + "'>[Back]</a><br><br>";
  append_page_footer();
  SendHTML_Content();
  SendHTML_Stop();
}

void ReportSDNotPresent(){
  SendHTML_Header();
  webpage += F("<h3>No SD Card present</h3>"); 
  webpage += F("<a href='/'>[Back]</a><br><br>");
  append_page_footer();
  SendHTML_Content();
  SendHTML_Stop();
}

void SD_file_download(String filename){
  if (bSdCardValid) { 
    FsFile download = SD.open("/"+filename);
    if (download) {
      server.sendHeader("Content-Type", "text/text");
      server.sendHeader("Content-Disposition", "attachment; filename="+filename);
      server.sendHeader("Connection", "close");
	  //server.streamFile(download, "application/octet-stream");
	  server.streamFile(download, "image/bmp");
      download.close();
    } else ReportFileNotPresent("download"); 
  } else ReportSDNotPresent();
}

void IncreaseRepeatButton(){
  // This can be for sure made into a universal function like IncreaseButton(Setting, Value)
  webpage += String("&nbsp;<a href='/settings/increpeat'><strong>&#8679;</strong></a>");
}

void DecreaseRepeatButton(){
  // This can be for sure made into a universal function like DecreaseButton(Setting, Value)
  webpage += String("&nbsp;<a href='/settings/decrepeat'><strong>&#8681;</strong></a>");
}

void ShowSettings() {
	append_page_header();
	webpage += "<h3>Current Settings</h3>";
	webpage += String("<p>Current File: ") +currentFolder+ FileNames[CurrentFileIndex];
	if (ImgInfo.bFixedTime) {
		webpage += String("<p>Fixed Image Time: ") + String(ImgInfo.nFixedImageTime) + " S";
	}
	else {
		webpage += String("<p>Column Time: ") + String(ImgInfo.nFrameHold) + " mS";
	}
	webpage += String("<p>Repeat Count: ") + String(ImgInfo.repeatCount);
	IncreaseRepeatButton();
	DecreaseRepeatButton();
	webpage += String("<p>LED Brightness: ") + String(LedInfo.nLEDBrightness);
	append_page_footer();
	server.send(200, "text/html", webpage);
}

void File_Upload(){
  append_page_header();
  webpage += F("<h3>Select File to Upload</h3>"); 
  webpage += F("<FORM action='/fupload' method='post' enctype='multipart/form-data'>");
  webpage += F("<input class='buttons' style='width:75%' type='file' name='fupload' id = 'fupload' value=''><br>");
  webpage += F("<br><button class='buttons' style='width:75%' type='submit'>Upload File</button><br>");
  webpage += F("<a href='/'>[Back]</a><br><br>");
  append_page_footer();
  server.send(200, "text/html",webpage);
}

// show on leds or display
// mode 0 is normal, mode 1 is prepare for LCD, mode 2 is reset to normal
void ShowLeds(int mode)
{
	static uint16_t* scrBuf = nullptr;
	static int col = 0;
	if (scrBuf == nullptr && mode == 0) {
		FastLED.show();
		return;
	}
	else if (mode == 0) {
		for (int ix = 0; ix < tft.height(); ++ix) {
			uint16_t color = tft.color565(leds[ix].r, leds[ix].g, leds[ix].b);
			uint16_t sbcolor;
			// the memory image colors are byte swapped
			swab(&color, &sbcolor, sizeof(uint16_t));
			scrBuf[ix] = sbcolor;
		}
		tft.pushRect(col, 0, 1, tft.height(), scrBuf);
		++col;
		if (col == tft.width())
			tft.fillScreen(TFT_BLACK);
		col = col % tft.width();
	}
	else if (mode == 1) {
		col = 0;
		tft.fillScreen(TFT_BLACK);
		FastLED.clearData();
		scrBuf = (uint16_t*)calloc(144, sizeof(uint16_t));
	}
	else if (mode == 2) {
		free(scrBuf);
		scrBuf = NULL;
	}
}
