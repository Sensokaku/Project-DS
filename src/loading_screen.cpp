#include <string>
#include "audio.h"
#include <nds.h>


uint16_t load_bar = 0;
char frames[4] = {'|', '/', '-', '\\'};
uint8_t curr_frame = 0;
// char frames[10] = {'⠋', '⠙', '⠹', '⠸', '⠼', '⠴', '⠦', '⠧', '⠇', '⠏'}
void startLoadingScreen()
{
    std::string songPath = "/project-ds/pcm/selector_ft_lp.pcm"; 
    playSong(songPath);
    updateSong();
    printf("\x1b[39m\x1b[23;0H  Loading...");

};

void loadingStep()
{
    load_bar++;
    if (load_bar == 200) {
        updateSong();
        load_bar = 0;
        curr_frame++;
        printf("\x1b[39m\x1b[23;0H%c", frames[curr_frame % 4]);
    }

}

void loadingStop()
{
    stopSong();
    consoleClear();
}