#include "MC_PULL_calibration.h"
#include "ws2812.h"

#include "Flash_saves.h"
#include "Motion_control.h"
#include "_bus_hardware.h"
#include "ams.h"
#include "ahub_bus.h"
#include "bambu_bus_ams.h"
#include "ADC_DMA.h"
#include "Debug_log.h"
#include <string.h>

WS2812_class SYS_RGB;
WS2812_class RGBOUT[4];

void RGB_init()
{
    SYS_RGB.init(1, GPIOD, GPIO_Pin_1);
    RGBOUT[0].init(2, GPIOA, GPIO_Pin_11);
    RGBOUT[1].init(2, GPIOA, GPIO_Pin_8);
    RGBOUT[2].init(2, GPIOB, GPIO_Pin_1);
    RGBOUT[3].init(2, GPIOB, GPIO_Pin_0);
}

void RGB_update()
{
    if (!(SYS_RGB.is_dirty() ||
          RGBOUT[0].is_dirty() || RGBOUT[1].is_dirty() ||
          RGBOUT[2].is_dirty() || RGBOUT[3].is_dirty()))
        return;

    static uint32_t last = 0u;

    uint32_t min_gap = time_hw_tpms;
    if (!min_gap) min_gap = 1u;

    const uint32_t now = time_ticks32();
    if (last != 0u && (uint32_t)(now - last) < min_gap)
        return;

    last = now;

    SYS_RGB.updata();
    RGBOUT[0].updata();
    RGBOUT[1].updata();
    RGBOUT[2].updata();
    RGBOUT[3].updata();
}

static uint8_t g_fil_dirty = 0;
static uint8_t g_loaded_ch = 0xFF;
static uint8_t g_state_dirty = 0;

static inline void ram_to_flashinfo(uint8_t fil, Flash_FilamentInfo* o)
{
    const _filament* f = &ams[BAMBU_BUS_AMS_NUM].filament[fil];

    memcpy(o->bambubus_filament_id, f->bambubus_filament_id, sizeof(o->bambubus_filament_id));
    o->color_R = f->color_R;
    o->color_G = f->color_G;
    o->color_B = f->color_B;
    o->color_A = f->color_A;
    o->temperature_min = f->temperature_min;
    o->temperature_max = f->temperature_max;
    memcpy(o->name, f->name, sizeof(o->name));
}

static inline void flashinfo_to_ram(uint8_t fil, const Flash_FilamentInfo* i)
{
    _filament* f = &ams[BAMBU_BUS_AMS_NUM].filament[fil];

    memcpy(f->bambubus_filament_id, i->bambubus_filament_id, sizeof(i->bambubus_filament_id));
    f->color_R = i->color_R;
    f->color_G = i->color_G;
    f->color_B = i->color_B;
    f->color_A = i->color_A;
    f->temperature_min = i->temperature_min;
    f->temperature_max = i->temperature_max;

    memset(f->name, 0, sizeof(f->name));
    memcpy(f->name, i->name, sizeof(i->name));
    f->name[sizeof(f->name) - 1u] = 0;
}

bool ams_datas_read()
{
    bool any = false;

    for (uint8_t fil = 0; fil < 4u; fil++)
    {
        Flash_FilamentInfo fi;
        if (Flash_AMS_filament_read(fil, &fi))
        {
            flashinfo_to_ram(fil, &fi);
            any = true;
        }
    }

    return any;
}

void ams_datas_set_need_to_save()
{
    g_fil_dirty = 0x0Fu;
}

void ams_datas_set_need_to_save_filament(uint8_t filament_idx)
{
    if (filament_idx >= 4u) return;
    g_fil_dirty |= (uint8_t)(1u << filament_idx);
}

void ams_state_set_loaded(uint8_t filament_ch)
{
    if (filament_ch >= 4u) return;
    if (g_loaded_ch != 0xFFu) return;
    g_loaded_ch = filament_ch;
    g_state_dirty = 1u;
}

void ams_state_set_unloaded(uint8_t filament_ch)
{
    if (g_loaded_ch == 0xFFu) return;
    if (filament_ch < 4u && g_loaded_ch != filament_ch) return;
    g_loaded_ch = 0xFFu;
    g_state_dirty = 1u;
}

uint8_t ams_state_get_loaded(void)
{
    return g_loaded_ch;
}

static void ams_state_save_run()
{
    if (!g_state_dirty) return;

    if (Flash_AMS_state_write(g_loaded_ch))
        g_state_dirty = 0u;
}

void ams_datas_save_run()
{
    if (!g_fil_dirty) return;

    uint8_t fil = 0xFFu;
    for (uint8_t i = 0; i < 4u; i++)
    {
        if (g_fil_dirty & (uint8_t)(1u << i))
        {
            fil = i;
            break;
        }
    }

    if (fil == 0xFFu) return;

    Flash_FilamentInfo now;
    ram_to_flashinfo(fil, &now);

    if (Flash_AMS_filament_write(fil, &now))
        g_fil_dirty &= (uint8_t)~(1u << fil);
}

int main(void)
{
    DEBUG("SystemInit...\n");
    SystemInit();
    SystemCoreClockUpdate();
    time_hw_init();

    __enable_irq();

    WWDG_DeInit();
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_WWDG, DISABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    GPIO_PinRemapConfig(GPIO_Remap_PD01, ENABLE);

    RGB_init();
    delay(10);

    SYS_RGB.set_RGB(0x10, 0x00, 0x00, 0);
    for (int i = 0; i < 4; i++) RGBOUT[i].set_RGB(0, 0, 0, 0);
    RGB_update();
    delay(50);

    DEBUG_init();
    ams_init();
    Flash_saves_init();

    ADC_DMA_init();
    ADC_DMA_wait_full();
    DEBUG("MC_PULL_calibration_boot...\n");
    MC_PULL_calibration_boot();
    ams_datas_read();

    {
        uint8_t ch = 0xFFu;
        if (Flash_AMS_state_read(&ch))
        {
            g_loaded_ch = ch;

            if (ch < 4u)
            {
                _ams* a = &ams[BAMBU_BUS_AMS_NUM];

                a->now_filament_num  = ch;
                a->filament_use_flag = 0x04;
                a->pressure          = 0x2B00;

                for (uint8_t i = 0; i < 4u; i++)
                    a->filament[i].motion = _filament_motion::idle;

                a->filament[ch].motion = _filament_motion::on_use;
            }
        }
    }

    Motion_control_init();
    bambubus_init();
    bus_init();

    DEBUG("START\n");
    
    const unsigned long IDLE_TIMEOUT = 60000; // 1 minute in milliseconds

    unsigned long lastActivityTime = time_ms64(); 

    //如果有进过料，再进行idle的时候，1分钟后 自动重启
    bool is_filament_sendout = false;

    while (1)
    {
        const ahubus_package_type   ahub_stu     = ahubus_run();
        const bambubus_package_type bambubus_stu = bambubus_run();
        bus_port_to_host.send_package();

        static int error = 0;

        if ((ahub_stu != ahubus_package_type::none) || (bambubus_stu != bambubus_package_type::none))
        {
            if ((ahub_stu != ahubus_package_type::error) || (bambubus_stu != bambubus_package_type::error))
            {
                error = 0;

                if (bambubus_stu == bambubus_package_type::heartbeat)
                {
                    SYS_RGB.set_RGB(0x38, 0x35, 0x32, 0);
                    bus_host_device_type = host_device_type_ams;
                }

                if (ahub_stu == ahubus_package_type::heartbeat)
                    bus_host_device_type = host_device_type_ahub;

                ams_datas_save_run();
                ams_state_save_run();
            }
            else
            {
                error = -1;
                SYS_RGB.set_RGB(0x10, 0x00, 0x00, 0);
            }
        }
        error = 0;
        Motion_control_run(error);
        RGB_update();
        
        bool onidle = true;
        {
             _ams* a = &ams[BAMBU_BUS_AMS_NUM];
             float  pwm_ratio[4] = {1.0f,1.0f,1.0f,1.0f};
             for (size_t i = 0; i < 4; i++)
             {
                if(a->filament[i].name[0]=='T' && a->filament[i].name[1]=='P' && a->filament[i].name[2]=='U')
                    pwm_ratio[i]=0.5f;
                if(a->filament[i].motion != _filament_motion::idle)
                {
                    if(!is_filament_sendout){
                        DEBUG("****in use****");
                    }
                    is_filament_sendout = true;
                    onidle = false;
                }
                    

             }
             
             Motion_control_setValue(pwm_ratio);
            
        }
        if(error==0)
        {
            if(!is_filament_sendout || !onidle){
                lastActivityTime = time_ms64();
            }
                
        }
        if(error==0)
        {   //启动为白灯
            //已经启动 为绿灯
            //空闲为 蓝灯 （等待重启）
            //重启过程 为 紫灯
            if(is_filament_sendout){
                SYS_RGB.set_RGB(0x00, 200, 0x00, 0);
            }
            if(is_filament_sendout && onidle){
                SYS_RGB.set_RGB(0x00, 100, 200, 0);
            }
             // 用当前时间减去最后一次活动时间，判断是否达到了阈值
            if (time_ms64() - lastActivityTime >= IDLE_TIMEOUT) {
                SYS_RGB.set_RGB(255, 0x00, 255, 0);
                DEBUG("System has been idle for 1 minute. Rebooting...");
                
                // 延时一下以确保上面的 Serial.println 能够完整发送到电脑
                delay(1000 * 10); 
                
                // 3. 触发核心软件复位 (CH32V / RISC-V 均适用)
                NVIC_SystemReset(); 
            }
        }
    }
}