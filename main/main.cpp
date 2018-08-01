#include "freertos/FreeRTOS.h"
#include "esp_system.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_partition.h"
#include "driver/i2s.h"
#include "esp_spiffs.h"
#include "nvs_flash.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h"
#include "esp_ota_ops.h"

extern "C"
{
#include "../components/odroid/odroid_settings.h"
#include "../components/odroid/odroid_audio.h"
#include "../components/odroid/odroid_input.h"
#include "../components/odroid/odroid_system.h"
#include "../components/odroid/odroid_display.h"
#include "../components/odroid/odroid_sdcard.h"

#include "../components/ugui/ugui.h"
}

// Stella
#include "Console.hxx"
#include "Cart.hxx"
#include "Props.hxx"
#include "MD5.hxx"
#include "Sound.hxx"
#include "OSystem.hxx"
#include "TIA.hxx"
#include "PropsSet.hxx"
#include "Switches.hxx"
#include "SoundSDL.hxx"

#include <dirent.h>
#include <string.h>
#include <ctype.h>

#define ESP32_PSRAM (0x3f800000)
const char* SD_BASE_PATH = "/sd";

#define AUDIO_SAMPLE_RATE (31400)

QueueHandle_t vidQueue;

#define STELLA_WIDTH 160
#define STELLA_HEIGHT 210
//uint16_t* framebuffer; //[STELLA_WIDTH * STELLA_HEIGHT * sizeof(uint16_t)];
//uint16_t framebuffer[STELLA_WIDTH * STELLA_HEIGHT];
uint8_t framebuffer[STELLA_WIDTH * STELLA_HEIGHT];
uint16_t pal16[256];

void videoTask(void *arg)
{
    while(1)
    {
        uint8_t* param;
        xQueuePeek(vidQueue, &param, portMAX_DELAY);
        //
        // if (param == (uint16_t*)1)
        //     break;

        //ili9341_write_frame_rectangleLE(0, 0, STELLA_WIDTH, STELLA_HEIGHT, framebuffer);
        memcpy(framebuffer, param, sizeof(framebuffer));
        ili9341_write_frame_atari2600(framebuffer, pal16);

        xQueueReceive(vidQueue, &param, portMAX_DELAY);

    }

    odroid_display_lock_sms_display();

    // Draw hourglass
    odroid_display_show_hourglass();

    odroid_display_unlock_sms_display();

    vTaskDelete(NULL);

    while (1) {}
}

// volatile bool test = true;
// volatile uint16_t test2 = true;

UG_GUI gui;
uint16_t* fb;

static void pset(UG_S16 x, UG_S16 y, UG_COLOR color)
{
    fb[y * 320 + x] = color;
}

static void window1callback(UG_MESSAGE* msg)
{
}

static void UpdateDisplay()
{
    UG_Update();
    ili9341_write_frame_rectangleLE(0, 0, 320, 240, fb);
}



// A utility function to swap two elements
inline static void swap(char** a, char** b)
{
    char* t = *a;
    *a = *b;
    *b = t;
}

static int strcicmp(char const *a, char const *b)
{
    for (;; a++, b++)
    {
        int d = tolower((int)*a) - tolower((int)*b);
        if (d != 0 || !*a) return d;
    }
}

//------
/* This function takes last element as pivot, places
   the pivot element at its correct position in sorted
    array, and places all smaller (smaller than pivot)
   to left of pivot and all greater elements to right
   of pivot */
static int partition (char* arr[], int low, int high)
{
    char* pivot = arr[high];    // pivot
    int i = (low - 1);  // Index of smaller element

    for (int j = low; j <= high- 1; j++)
    {
        // If current element is smaller than or
        // equal to pivot
        if (strcicmp(arr[j], pivot) < 0) //(arr[j] <= pivot)
        {
            i++;    // increment index of smaller element
            swap(&arr[i], &arr[j]);
        }
    }
    swap(&arr[i + 1], &arr[high]);
    return (i + 1);
}

/* The main function that implements QuickSort
 arr[] --> Array to be sorted,
  low  --> Starting index,
  high  --> Ending index */
static void quickSort(char* arr[], int low, int high)
{
    if (low < high)
    {
        /* pi is partitioning index, arr[p] is now
           at right place */
        int pi = partition(arr, low, high);

        // Separately sort elements before
        // partition and after partition
        quickSort(arr, low, pi - 1);
        quickSort(arr, pi + 1, high);
    }
}

static void SortFiles(char** files, int count)
{
    int n = count;
    bool swapped = true;

    if (count > 1)
    {
        quickSort(files, 0, count - 1);
    }
}

int GetFiles(const char* path, const char* extension, char*** filesOut)
{
    //printf("GetFiles: path='%s', extension='%s'\n", path, extension);
    //OpenSDCard();

    const int MAX_FILES = 100;

    int count = 0;
    //char** result = (char**)heap_caps_malloc(MAX_FILES * sizeof(void*), MALLOC_CAP_SPIRAM);
    char** result = (char**)malloc(MAX_FILES * sizeof(void*));
    if (!result) abort();

    //*filesOut = result;

    DIR *dir = opendir(path);
    if( dir == NULL )
    {
        printf("opendir failed.\n");
        abort();
    }

    int extensionLength = strlen(extension);
    if (extensionLength < 1) abort();


    char* temp = (char*)malloc(extensionLength + 1);
    if (!temp) abort();

    memset(temp, 0, extensionLength + 1);


    // Print files
    struct dirent *entry;
    while((entry=readdir(dir)) != NULL)
    {
        //printf("File: %s\n", entry->d_name);
        size_t len = strlen(entry->d_name);

        bool skip = false;

        // ignore 'hidden' files (MAC)
        if (entry->d_name[0] == '.') skip = true;

        // ignore BIOS file(s)
        char* lowercase = (char*)malloc(len + 1);
        if (!lowercase) abort();

        lowercase[len] = 0;
        for (int i = 0; i < len; ++i)
        {
            lowercase[i] = tolower((int)entry->d_name[i]);
        }
        if (strcmp(lowercase, "bios.col") == 0) skip = true;

        free(lowercase);


        memset(temp, 0, extensionLength + 1);
        if (!skip)
        {
            for (int i = 0; i < extensionLength; ++i)
            {
                temp[i] = tolower((int)entry->d_name[len - extensionLength + i]);
            }

            if (len > extensionLength)
            {
                if (strcmp(temp, extension) == 0)
                {
                    //result[count] = (char*)heap_caps_malloc(len + 1, MALLOC_CAP_SPIRAM);
                    result[count] = (char*)malloc(len + 1);
                    if (!result[count])
                    {
                        abort();
                    }

                    strcpy(result[count], entry->d_name);
                    ++count;

                    if (count >= MAX_FILES) break;
                }
            }
        }
    }

    free(temp);
    //CloseSDCard();


    SortFiles(result, count);

#if 0
    for (int i = 0; i < count; ++i)
    {
        printf("GetFiles: %d='%s'\n", i, result[i]);
    }
#endif

    *filesOut = result;
    return count;
}

void FreeFiles(char** files, int count)
{
    for (int i = 0; i < count; ++i)
    {
        free(files[i]);
    }

    free(files);
}





#define MAX_OBJECTS 20
#define ITEM_COUNT  10

UG_WINDOW window1;
UG_BUTTON button1;
UG_TEXTBOX textbox[ITEM_COUNT];
UG_OBJECT objbuffwnd1[MAX_OBJECTS];



void DrawPage(char** files, int fileCount, int currentItem)
{
    int page = currentItem / ITEM_COUNT;
    page *= ITEM_COUNT;

    // Reset all text boxes
    for (int i = 0; i < ITEM_COUNT; ++i)
    {
        uint16_t id = TXB_ID_0 + i;
        //UG_TextboxSetForeColor(&window1, id, C_BLACK);
        UG_TextboxSetText(&window1, id, "");
    }

	if (fileCount < 1)
	{
		const char* text = "(empty)";

        uint16_t id = TXB_ID_0 + (ITEM_COUNT / 2);
        UG_TextboxSetText(&window1, id, (char*)text);

        UpdateDisplay();
	}
	else
	{
        char* displayStrings[ITEM_COUNT];
        for(int i = 0; i < ITEM_COUNT; ++i)
        {
            displayStrings[i] = NULL;
        }

	    for (int line = 0; line < ITEM_COUNT; ++line)
	    {
			if (page + line >= fileCount) break;

            uint16_t id = TXB_ID_0 + line;

	        if ((page) + line == currentItem)
	        {
	            UG_TextboxSetForeColor(&window1, id, C_BLACK);
                UG_TextboxSetBackColor(&window1, id, C_YELLOW);
	        }
	        else
	        {
	            UG_TextboxSetForeColor(&window1, id, C_BLACK);
                UG_TextboxSetBackColor(&window1, id, C_WHITE);
	        }

			char* fileName = files[page + line];
			if (!fileName) abort();

			displayStrings[line] = (char*)malloc(strlen(fileName) + 1);
            strcpy(displayStrings[line], fileName);
            displayStrings[line][strlen(fileName) - 4] = 0;

	        UG_TextboxSetText(&window1, id, displayStrings[line]);
	    }

        UpdateDisplay();

        for(int i = 0; i < ITEM_COUNT; ++i)
        {
            free(displayStrings[i]);
        }
	}


}

static const char* ChooseFile()
{
    const char* result = NULL;

    //fb = (uint16_t*)heap_caps_malloc(320 * 240 * 2, MALLOC_CAP_SPIRAM);
    //if (!fb) abort();
    fb = (uint16_t*)ESP32_PSRAM;

    UG_Init(&gui, pset, 320, 240);

    UG_WindowCreate(&window1, objbuffwnd1, MAX_OBJECTS, window1callback);

    UG_WindowSetTitleText(&window1, "SELECT A FILE");
    UG_WindowSetTitleTextFont(&window1, &FONT_10X16);
    UG_WindowSetTitleTextAlignment(&window1, ALIGN_CENTER);


    UG_S16 innerWidth = UG_WindowGetInnerWidth(&window1);
    UG_S16 innerHeight = UG_WindowGetInnerHeight(&window1);
    UG_S16 titleHeight = UG_WindowGetTitleHeight(&window1);
    UG_S16 textHeight = (innerHeight) / ITEM_COUNT;


    for (int i = 0; i < ITEM_COUNT; ++i)
    {
        uint16_t id = TXB_ID_0 + i;
        UG_S16 top = i * textHeight;
        UG_TextboxCreate(&window1, &textbox[i], id, 0, top, innerWidth, top + textHeight - 1);
        UG_TextboxSetFont(&window1, id, &FONT_8X12);
        UG_TextboxSetForeColor(&window1, id, C_BLACK);
        UG_TextboxSetAlignment(&window1, id, ALIGN_CENTER);
        //UG_TextboxSetText(&window1, id, "ABCDEFGHabcdefg");
    }

    UG_WindowShow(&window1);
    UpdateDisplay();


    const char* path = "/sd/roms/a26";
    char** files;

    int fileCount =  GetFiles(path, ".a26", &files);


// Selection
    int currentItem = 0;
    DrawPage(files, fileCount, currentItem);

    odroid_gamepad_state previousState;
    odroid_input_gamepad_read(&previousState);

    while (true)
    {
		odroid_gamepad_state state;
		odroid_input_gamepad_read(&state);

        int page = currentItem / 10;
        page *= 10;

		if (fileCount > 0)
		{
	        if(!previousState.values[ODROID_INPUT_DOWN] && state.values[ODROID_INPUT_DOWN])
	        {
	            if (fileCount > 0)
				{
					if (currentItem + 1 < fileCount)
		            {
		                ++currentItem;
		                DrawPage(files, fileCount, currentItem);
		            }
					else
					{
						currentItem = 0;
		                DrawPage(files, fileCount, currentItem);
					}
				}
	        }
	        else if(!previousState.values[ODROID_INPUT_UP] && state.values[ODROID_INPUT_UP])
	        {
	            if (fileCount > 0)
				{
					if (currentItem > 0)
		            {
		                --currentItem;
		                DrawPage(files, fileCount, currentItem);
		            }
					else
					{
						currentItem = fileCount - 1;
						DrawPage(files, fileCount, currentItem);
					}
				}
	        }
	        else if(!previousState.values[ODROID_INPUT_RIGHT] && state.values[ODROID_INPUT_RIGHT])
	        {
	            if (fileCount > 0)
				{
					if (page + 10 < fileCount)
		            {
		                currentItem = page + 10;
		                DrawPage(files, fileCount, currentItem);
		            }
					else
					{
						currentItem = 0;
						DrawPage(files, fileCount, currentItem);
					}
				}
	        }
	        else if(!previousState.values[ODROID_INPUT_LEFT] && state.values[ODROID_INPUT_LEFT])
	        {
	            if (fileCount > 0)
				{
					if (page - 10 >= 0)
		            {
		                currentItem = page - 10;
		                DrawPage(files, fileCount, currentItem);
		            }
					else
					{
						currentItem = page;
						while (currentItem + 10 < fileCount)
						{
							currentItem += 10;
						}

		                DrawPage(files, fileCount, currentItem);
					}
				}
	        }
	        else if(!previousState.values[ODROID_INPUT_A] && state.values[ODROID_INPUT_A])
	        {
	            size_t fullPathLength = strlen(path) + 1 + strlen(files[currentItem]) + 1;

	            //char* fullPath = (char*)heap_caps_malloc(fullPathLength, MALLOC_CAP_SPIRAM);
                char* fullPath = (char*)malloc(fullPathLength);
	            if (!fullPath) abort();

	            strcpy(fullPath, path);
	            strcat(fullPath, "/");
	            strcat(fullPath, files[currentItem]);

	            result = fullPath;
                break;
	        }
		}

        previousState = state;
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    FreeFiles(files, fileCount);

    //free(fb);
    return result;
}

static Console *console = 0;
static Cartridge *cartridge = 0;
static Settings *settings = 0;
static OSystem* osystem;


void stella_init(const char* filename)
{
    printf("%s: HEAP:0x%x (%#08x)\n",
      __func__,
      esp_get_free_heap_size(),
      heap_caps_get_free_size(MALLOC_CAP_DMA));

    FILE* fp = fopen(filename, "rb");

    fseek(fp, 0, SEEK_END);
    size_t size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    //void* data = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    void* data = malloc(size);
    if (!data) abort();

    size_t count = fread(data, 1, size, fp);
    if (count != size) abort();

    fclose(fp); fp = NULL;


    string cartMD5 = MD5((uInt8*)data, (uInt32)size);

    osystem = new OSystem();
    Properties props;
    osystem->propSet().getMD5(cartMD5, props);

    // Load the cart
    string cartType = props.get(Cartridge_Type);
    string cartId;//, romType("AUTO-DETECT");

    printf("%s: HEAP:0x%x (%#08x)\n",
      __func__,
      esp_get_free_heap_size(),
      heap_caps_get_free_size(MALLOC_CAP_DMA));
    settings = new Settings(osystem);
    settings->setValue("romloadcount", false);
    cartridge = Cartridge::create((const uInt8*)data, (uInt32)size, cartMD5, cartType, cartId, *osystem, *settings);

    if(cartridge == 0)
    {
        printf("Stella: Failed to load cartridge.\n");
        abort();
    }

    // Create the console
    console = new Console(osystem, cartridge, props);
    osystem->myConsole = console;

    // Init sound and video
    console->initializeVideo();
    console->initializeAudio();

    // Get the ROM's width and height
    TIA& tia = console->tia();
    int videoWidth = tia.width();
    int videoHeight = tia.height();

    printf("videoWidth = %d, videoHeight = %d\n", videoWidth, videoHeight);
    //framebuffer = (uint16_t*)malloc(videoWidth * videoHeight * 2);
    //if (!framebuffer) abort();

    const uint32_t *palette = console->getPalette(0);
    for (int i = 0; i < 256; ++i)
    {
        uint32_t color = palette[i];

        uint16_t r = (color >> 16) & 0xff;
        uint16_t g = (color >> 8) & 0xff;
        uint16_t b = (color >> 0) & 0xff;


        //rrrr rggg gggb bbbb
        uint16_t rgb565 = ((r << 8) & 0xf800) | ((g << 3) & 0x7e00) | (b >> 3);
        //rgb565 = (rgb565 >> 8) | (rgb565 << 8);
        pal16[i] = rgb565;
    }
}

static int32_t* sampleBuffer; //[2048];
void stella_step(odroid_gamepad_state* gamepad)
{
    //EMULATE
    TIA& tia = console->tia();
    tia.update();

    //VIDEO
    //Get the frame info from stella
    //int videoWidth = tia.width();
    //int videoHeight = tia.height();

    // //Copy the frame from stella
    // for (int i = 0; i < videoHeight * videoWidth; ++i)
    // {
    //     framebuffer[i] = pal16[tia.currentFrameBuffer()[i]];
    // }

    //Process one frame of audio from stella
    static uint32_t tiaSamplesPerFrame = (uint32_t)(31400.0f/console->getFramerate());

    if (sampleBuffer == NULL)
    {
        sampleBuffer = (int32_t*)malloc(tiaSamplesPerFrame * sizeof(int32_t));
        if (!sampleBuffer) abort();
    }

    SoundSDL *sound = (SoundSDL*)&osystem->sound();
    sound->processFragment((int16_t*)sampleBuffer, tiaSamplesPerFrame);

    odroid_audio_submit((int16_t*)sampleBuffer, tiaSamplesPerFrame);
}

extern "C" void app_main()
{
    printf("stella-go started.\n");

    printf("HEAP:0x%x (%#08x)\n",
      esp_get_free_heap_size(),
      heap_caps_get_free_size(MALLOC_CAP_DMA));


    nvs_flash_init();

    odroid_system_init();
    odroid_input_gamepad_init();
    odroid_input_battery_level_init();

    ili9341_prepare();

    ili9341_init();
    ili9341_clear(0x0000);


    // Open SD card
    esp_err_t r = odroid_sdcard_open(SD_BASE_PATH);
    if (r != ESP_OK)
    {
        odroid_display_show_sderr(ODROID_SD_ERR_NOCARD);
        abort();
    }


    const char* romfile = ChooseFile();
    printf("%s: filename='%s'\n", __func__, romfile);


    ili9341_clear(0x0000);
    stella_init(romfile);

    odroid_audio_init(AUDIO_SAMPLE_RATE);

    vidQueue = xQueueCreate(1, sizeof(uint16_t*));
    xTaskCreatePinnedToCore(&videoTask, "videoTask", 1024 * 4, NULL, 5, NULL, 1);


    uint startTime;
    uint stopTime;
    uint totalElapsedTime = 0;
    int frame = 0;
    int renderFrames = 0;
    uint16_t muteFrameCount = 0;
    uint16_t powerFrameCount = 0;

    odroid_gamepad_state last_gamepad;
    odroid_input_gamepad_read(&last_gamepad);

    while(1)
    {
        startTime = xthal_get_ccount();


        odroid_gamepad_state gamepad;
        odroid_input_gamepad_read(&gamepad);

        if (last_gamepad.values[ODROID_INPUT_MENU] &&
            !gamepad.values[ODROID_INPUT_MENU])
        {
            esp_restart();
        }

        stella_step(&gamepad);
        //printf("stepped.\n");

        //if (!(frame & 1))
        UBaseType_t count = uxQueueSpacesAvailable(vidQueue);
        if (count > 0)
        {
            TIA& tia = console->tia();
            uint8_t* fb = tia.currentFrameBuffer();

            xQueueSend(vidQueue, &fb, portMAX_DELAY);

            ++renderFrames;
        }

        last_gamepad = gamepad;


        // end of frame
        stopTime = xthal_get_ccount();

        //previousState = joystick;

        odroid_battery_state battery;
        odroid_input_battery_level_read(&battery);


        int elapsedTime;
        if (stopTime > startTime)
          elapsedTime = (stopTime - startTime);
        else
          elapsedTime = ((uint64_t)stopTime + (uint64_t)0xffffffff) - (startTime);

        totalElapsedTime += elapsedTime;
        ++frame;

        if (frame == 60)
        {
          float seconds = totalElapsedTime / (CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ * 1000000.0f);
          float fps = frame / seconds;
          float renderFps = renderFrames / seconds;

          printf("HEAP:0x%x (%#08x), SIM:%f, REN:%f, BATTERY:%d [%d]\n",
            esp_get_free_heap_size(),
            heap_caps_get_free_size(MALLOC_CAP_DMA),
            fps,
            renderFps,
            battery.millivolts,
            battery.percentage);

          frame = 0;
          renderFrames = 0;
          totalElapsedTime = 0;
        }
    }
}
