/* gameplaySP
 *
 * Copyright (C) 2006 Exophase <exophase@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "common.h"

#ifdef PSP_BUILD

void vblank_interrupt_handler(u32 sub, u32 *parg);

PSP_HEAP_SIZE_MAX();

#endif

timer_type timer[4];

//debug_state current_debug_state = COUNTDOWN_BREAKPOINT;
//debug_state current_debug_state = PC_BREAKPOINT;
u32 breakpoint_value = 0x7c5000;
debug_state current_debug_state = RUN;
//u32 breakpoint_value = 0;

frameskip_type current_frameskip_type = auto_frameskip;
u32 frameskip_value = 4;
u32 random_skip = 0;
u32 global_cycles_per_instruction = 3;

u32 skip_next_frame = 0;

u32 frameskip_counter = 0;

u32 cpu_ticks = 0;
u32 frame_ticks = 0;

u32 execute_cycles = 960;
s32 video_count = 960;
u32 ticks;

u32 arm_frame = 0;
u32 thumb_frame = 0;
u32 last_frame = 0;

u32 cycle_memory_access = 0;
u32 cycle_pc_relative_access = 0;
u32 cycle_sp_relative_access = 0;
u32 cycle_block_memory_access = 0;
u32 cycle_block_memory_sp_access = 0;
u32 cycle_block_memory_words = 0;
u32 cycle_dma16_words = 0;
u32 cycle_dma32_words = 0;
u32 flush_ram_count = 0;
u32 gbc_update_count = 0;
u32 oam_update_count = 0;

u32 synchronize_flag = 1;

u32 update_backup_flag = 1;
u32 clock_speed = 333;
volatile u8 main_path[512];

#define check_count(count_var)                                                \
  if(count_var < execute_cycles)                                              \
    execute_cycles = count_var;                                               \

#define check_timer(timer_number)                                             \
  if(timer[timer_number].status == TIMER_PRESCALE)                            \
    check_count(timer[timer_number].count);                                   \

#define update_timer(timer_number)                                            \
  if(timer[timer_number].status != TIMER_INACTIVE)                            \
  {                                                                           \
    if(timer[timer_number].status != TIMER_CASCADE)                           \
    {                                                                         \
      timer[timer_number].count -= execute_cycles;                            \
      io_registers[REG_TM##timer_number##D] =                                 \
       -(timer[timer_number].count >> timer[timer_number].prescale);          \
    }                                                                         \
                                                                              \
    if(timer[timer_number].count <= 0)                                        \
    {                                                                         \
      if(timer[timer_number].irq == TIMER_TRIGGER_IRQ)                        \
        irq_raised |= IRQ_TIMER##timer_number;                                \
                                                                              \
      if((timer_number != 3) &&                                               \
       (timer[timer_number + 1].status == TIMER_CASCADE))                     \
      {                                                                       \
        timer[timer_number + 1].count--;                                      \
        io_registers[REG_TM0D + (timer_number + 1) * 2] =                     \
         -(timer[timer_number + 1].count);                                    \
      }                                                                       \
                                                                              \
      if(timer_number < 2)                                                    \
      {                                                                       \
        if(timer[timer_number].direct_sound_channels & 0x01)                  \
          sound_timer(timer[timer_number].frequency_step, 0);                 \
                                                                              \
        if(timer[timer_number].direct_sound_channels & 0x02)                  \
          sound_timer(timer[timer_number].frequency_step, 1);                 \
      }                                                                       \
                                                                              \
      timer[timer_number].count +=                                            \
       (timer[timer_number].reload << timer[timer_number].prescale);          \
    }                                                                         \
  }                                                                           \

u8 *file_ext[] = { ".gba", ".bin", ".zip", NULL };

void init_main()
{
  u32 i;

  skip_next_frame = 0;

  for(i = 0; i < 4; i++)
  {
    dma[i].start_type = DMA_INACTIVE;
    dma[i].direct_sound_channel = DMA_NO_DIRECT_SOUND;
    timer[i].status = TIMER_INACTIVE;
    timer[i].reload = 0x10000;
    timer[i].stop_cpu_ticks = 0;
  }

  timer[0].direct_sound_channels = TIMER_DS_CHANNEL_BOTH;
  timer[1].direct_sound_channels = TIMER_DS_CHANNEL_NONE;

  cpu_ticks = 0;
  frame_ticks = 0;

  execute_cycles = 960;
  video_count = 960;

  flush_translation_cache_rom();
  flush_translation_cache_ram();
  flush_translation_cache_bios();
}

int main(int argc, char *argv[])
{
  u32 i;
  u32 vcount = 0;
  u32 ticks;
  u32 dispstat;
  u8 load_filename[512];

#ifdef PSP_BUILD
  sceKernelRegisterSubIntrHandler(PSP_VBLANK_INT, 0,
   vblank_interrupt_handler, NULL);
  sceKernelEnableSubIntr(PSP_VBLANK_INT, 0);
#else
  freopen("CON", "wb", stdout);
#endif

  init_gamepak_buffer();

  // Copy the directory path of the executable into main_path
  getcwd(main_path, 512);
  load_config_file();

  gamepak_filename[0] = 0;

  if(load_bios("gba_bios.bin") == -1)
  {
#ifdef PSP_BUILD
    gui_action_type gui_action = CURSOR_NONE;

    printf("Sorry, but gpSP requires a Gameboy Advance BIOS image to run\n");
    printf("correctly. Make sure to get an authentic one (search the web,\n");
    printf("beg other people if you want, but don't hold me accountable\n");
    printf("if you get hated or banned for it), it'll be exactly 16384\n");
    printf("bytes large and should have the following md5sum value:\n\n");
    printf("a860e8c0b6d573d191e4ec7db1b1e4f6\n\n");
    printf("Other BIOS files might work either partially completely, I\n");
    printf("really don't know.\n\n");
    printf("When you do get it name it gba_bios.bin and put it in the\n");
    printf("same directory as this EBOOT.\n\n");
    printf("Good luck. Press any button to exit.\n");

    while(gui_action == CURSOR_NONE)
    {
      gui_action = get_gui_input();
      delay_us(15000);
    }

    quit();
#endif
  }

#ifdef PSP_BUILD
  delay_us(2500000);
#endif

  init_main();
  init_sound();

  init_video();
  init_input();

  video_resolution_large();

  if(argc > 1)
  {
    if(load_gamepak(argv[1]) == -1)
    {
      printf("Failed to load gamepak %s, exiting.\n", load_filename);
      exit(-1);
    }

    set_gba_resolution(screen_scale);
    video_resolution_small();

    init_cpu();
    init_memory();
  }
  else
  {
    if(load_file(file_ext, load_filename) == -1)
    {
      menu(copy_screen());
    }
    else
    {
      if(load_gamepak(load_filename) == -1)
      {
        printf("Failed to load gamepak %s, exiting.\n", load_filename);
        delay_us(5000000);
        exit(-1);
      }

      set_gba_resolution(screen_scale);
      video_resolution_small();

      init_cpu();
      init_memory();
    }
  }

  last_frame = 0;

  // We'll never actually return from here.

#ifdef PSP_BUILD
  execute_arm_translate(execute_cycles);
#else
  execute_arm_translate(execute_cycles);
//  execute_arm(execute_cycles);
#endif
  return 0;
}

void print_memory_stats(u32 *counter, u32 *region_stats, u8 *stats_str)
{
  u32 other_region_counter = region_stats[0x1] + region_stats[0xE] + region_stats[0xF];
  u32 rom_region_counter = region_stats[0x8] + region_stats[0x9] + region_stats[0xA] +
   region_stats[0xB] + region_stats[0xC] + region_stats[0xD];
  u32 _counter = *counter;

  printf("memory access stats: %s (out of %d)\n", stats_str, _counter);
  printf("bios: %f%%\tiwram: %f%%\tewram: %f%%\tvram: %f\n",
   region_stats[0x0] * 100.0 / _counter, region_stats[0x3] * 100.0 / _counter,
   region_stats[0x2] * 100.0 / _counter, region_stats[0x6] * 100.0 / _counter);

  printf("oam: %f%%\tpalette: %f%%\trom: %f%%\tother: %f%%\n",
   region_stats[0x7] * 100.0 / _counter, region_stats[0x5] * 100.0 / _counter,
   rom_region_counter * 100.0 / _counter, other_region_counter * 100.0 / _counter);

  *counter = 0;
  memset(region_stats, 0, sizeof(u32) * 16);
}

u32 update_gba()
{
  irq_type irq_raised = IRQ_NONE;
  cpu_ticks += execute_cycles;

  if(gbc_sound_update)
  {
    gbc_update_count++;
    update_gbc_sound(cpu_ticks);
    gbc_sound_update = 0;
  }

  update_timer(0);
  update_timer(1);
  update_timer(2);
  update_timer(3);

  video_count -= execute_cycles;

  if(video_count <= 0)
  {
    u32 vcount = io_registers[REG_VCOUNT];
    u32 dispstat = io_registers[REG_DISPSTAT];

    if((dispstat & 0x02) == 0)
    {
      // Transition from hrefresh to hblank
      video_count += 272;
      dispstat |= 0x02;

      if((dispstat & 0x01) == 0)
      {
        u32 i;
        if(oam_update)
          oam_update_count++;

        update_scanline();


        // If in visible area also fire HDMA
        for(i = 0; i < 4; i++)
        {
          if(dma[i].start_type == DMA_START_HBLANK)
            dma_transfer(dma + i);
        }
      }

      if(dispstat & 0x10)
        irq_raised |= IRQ_HBLANK;
    }
    else
    {
      // Transition from hblank to next line
      video_count += 960;
      dispstat &= ~0x02;

      vcount++;

      if(vcount == 160)
      {
        // Transition from vrefresh to vblank
        u32 i;

        dispstat |= 0x01;
        if(dispstat & 0x8)
        {
          irq_raised |= IRQ_VBLANK;
        }

        affine_reference_x[0] =
         (s32)(address32(io_registers, 0x28) << 4) >> 4;
        affine_reference_y[0] =
         (s32)(address32(io_registers, 0x2C) << 4) >> 4;
        affine_reference_x[1] =
         (s32)(address32(io_registers, 0x38) << 4) >> 4;
        affine_reference_y[1] =
         (s32)(address32(io_registers, 0x3C) << 4) >> 4;

        for(i = 0; i < 4; i++)
        {
          if(dma[i].start_type == DMA_START_VBLANK)
            dma_transfer(dma + i);
        }
      }
      else

      if(vcount == 228)
      {
        // Transition from vblank to next screen
        dispstat &= ~0x01;
        frame_ticks++;

#ifndef PSP_BUILD
/*        printf("frame update (%x), %d instructions total, %d RAM flushes\n",
         reg[REG_PC], instruction_count - last_frame, flush_ram_count);
        last_frame = instruction_count;
        print_memory_stats(&memory_reads_u8, memory_region_access_read_u8,
         "unsigned 8bit read");
        print_memory_stats(&memory_reads_s8, memory_region_access_read_s8,
         "signed 8bit read");
        print_memory_stats(&memory_reads_u16, memory_region_access_read_u16,
         "unsigned 16bit read");
        print_memory_stats(&memory_reads_s16, memory_region_access_read_s16,
         "signed 16bit read");
        print_memory_stats(&memory_reads_u32, memory_region_access_read_u32,
         "32bit read");
        print_memory_stats(&memory_writes_u8, memory_region_access_write_u8,
         "8bit write");
        print_memory_stats(&memory_writes_u16, memory_region_access_write_u16,
         "16bit write");
        print_memory_stats(&memory_writes_u32, memory_region_access_write_u32,
         "32bit write");
        printf("%d gbc audio updates\n", gbc_update_count);
        printf("%d oam updates\n", oam_update_count); */
        gbc_update_count = 0;
        oam_update_count = 0;
        flush_ram_count = 0;
#endif

        if(update_input())
          return execute_cycles;

        update_gbc_sound(cpu_ticks);
        synchronize();
        update_screen();
        if(update_backup_flag)
          update_backup();

        process_cheats();

        vcount = 0;
      }

      if(vcount == (dispstat >> 8))
      {
        // vcount trigger
        dispstat |= 0x04;
        if(dispstat & 0x20)
        {
          irq_raised |= IRQ_VCOUNT;
        }
      }
      else
      {
        dispstat &= ~0x04;
      }

      io_registers[REG_VCOUNT] = vcount;
    }
    io_registers[REG_DISPSTAT] = dispstat;
  }

  if(irq_raised)
    raise_interrupt(irq_raised);

  execute_cycles = video_count;

  check_timer(0);
  check_timer(1);
  check_timer(2);
  check_timer(3);

  return execute_cycles;
}

u64 last_screen_timestamp = 0;
u32 frame_speed = 15000;

#ifdef PSP_BUILD

u32 real_frame_count = 0;
u32 virtual_frame_count = 0;
u32 num_skipped_frames = 0;

void vblank_interrupt_handler(u32 sub, u32 *parg)
{
  real_frame_count++;
}

void synchronize()
{
  char char_buffer[64];
  u64 new_ticks, time_delta;
  s32 used_frameskip = frameskip_value;

  if(!synchronize_flag)
  {
    print_string("--FF--", 0xFFFF, 0x000, 0, 0);
    used_frameskip = 4;
    virtual_frame_count = real_frame_count - 1;
  }

  skip_next_frame = 0;

  virtual_frame_count++;

  if(real_frame_count >= virtual_frame_count)
  {
    if((real_frame_count > virtual_frame_count) &&
     (current_frameskip_type == auto_frameskip) &&
     (num_skipped_frames < frameskip_value))
    {
      skip_next_frame = 1;
      num_skipped_frames++;
    }
    else
    {
      virtual_frame_count = real_frame_count;
      num_skipped_frames = 0;
    }

    // Here so that the home button return will eventually work.
    // If it's not running fullspeed anyway this won't really hurt
    // it much more.

    delay_us(1);
  }
  else
  {
    if(synchronize_flag)
      sceDisplayWaitVblankStart();
  }

  if(current_frameskip_type == manual_frameskip)
  {
    frameskip_counter = (frameskip_counter + 1) %
     (used_frameskip + 1);
    if(random_skip)
    {
      if(frameskip_counter != (rand() % (used_frameskip + 1)))
        skip_next_frame = 1;
    }
    else
    {
      if(frameskip_counter)
        skip_next_frame = 1;
    }
  }

/*  sprintf(char_buffer, "%08d %08d %d %d %d\n",
   real_frame_count, virtual_frame_count, num_skipped_frames,
   real_frame_count - virtual_frame_count, skip_next_frame);
  print_string(char_buffer, 0xFFFF, 0x0000, 0, 10); */

/*
    sprintf(char_buffer, "%02d %02d %06d %07d", frameskip, (u32)ms_needed,
     ram_translation_ptr - ram_translation_cache, rom_translation_ptr -
     rom_translation_cache);
    print_string(char_buffer, 0xFFFF, 0x0000, 0, 0);
*/
}

#else

u32 ticks_needed_total = 0;
float us_needed = 0.0;
u32 frames = 0;
const u32 frame_interval = 60;

void synchronize()
{
  u64 new_ticks;
  u64 time_delta;
  char char_buffer[64];

  get_ticks_us(&new_ticks);
  time_delta = new_ticks - last_screen_timestamp;
  last_screen_timestamp = new_ticks;
  ticks_needed_total += time_delta;

  skip_next_frame = 0;

  if((time_delta < frame_speed) && synchronize_flag)
  {
    delay_us(frame_speed - time_delta);
  }

  frames++;

  if(frames == frame_interval)
  {
    us_needed = (float)ticks_needed_total / frame_interval;
    ticks_needed_total = 0;
    frames = 0;
  }

  if(current_frameskip_type == manual_frameskip)
  {
    frameskip_counter = (frameskip_counter + 1) %
     (frameskip_value + 1);
    if(random_skip)
    {
      if(frameskip_counter != (rand() % (frameskip_value + 1)))
        skip_next_frame = 1;
    }
    else
    {
      if(frameskip_counter)
        skip_next_frame = 1;
    }
  }

  if(synchronize_flag == 0)
    print_string("--FF--", 0xFFFF, 0x000, 0, 0);

  sprintf(char_buffer, "gpSP: %.1fms %.1ffps", us_needed / 1000.0,
   1000000.0 / us_needed);
  SDL_WM_SetCaption(char_buffer, "gpSP");

/*
    sprintf(char_buffer, "%02d %02d %06d %07d", frameskip, (u32)ms_needed,
     ram_translation_ptr - ram_translation_cache, rom_translation_ptr -
     rom_translation_cache);
    print_string(char_buffer, 0xFFFF, 0x0000, 0, 0);
*/
}

#endif

void quit()
{
  if(!update_backup_flag)
    update_backup_force();

//  sound_exit(0);

#ifdef PSP_BUILD
    sceKernelExitGame();
#else
  SDL_Quit();
  exit(0);
#endif
}

void reset_gba()
{
  init_main();
  init_memory();
  init_cpu();
  reset_sound();
}

#ifdef PSP_BUILD

u32 file_length(u8 *filename, s32 dummy)
{
  SceIoStat stats;
  sceIoGetstat(filename, &stats);
  return stats.st_size;
}

void delay_us(u32 us_count)
{
  sceKernelDelayThread(us_count);
}

void get_ticks_us(u64 *tick_return)
{
  u64 ticks;
  sceRtcGetCurrentTick(&ticks);

  *tick_return = (ticks * 1000000) / sceRtcGetTickResolution();
}

#else

u32 file_length(u8 *dummy, FILE *fp)
{
  u32 length;

  fseek(fp, 0, SEEK_END);
  length = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  return length;
}

void delay_us(u32 us_count)
{
  SDL_Delay(us_count / 1000);
}

void get_ticks_us(u64 *ticks_return)
{
  *ticks_return = (SDL_GetTicks() * 1000);
}

#endif

void change_ext(u8 *src, u8 *buffer, u8 *extension)
{
  u8 *dot_position;
  strcpy(buffer, src);
  dot_position = strrchr(buffer, '.');

  if(dot_position)
    strcpy(dot_position, extension);
}

#define main_savestate_builder(type)                                          \
void main_##type##_savestate(file_tag_type savestate_file)                    \
{                                                                             \
  file_##type##_variable(savestate_file, cpu_ticks);                          \
  file_##type##_variable(savestate_file, execute_cycles);                     \
  file_##type##_variable(savestate_file, video_count);                        \
  file_##type##_array(savestate_file, timer);                                 \
}                                                                             \

main_savestate_builder(read);
main_savestate_builder(write_mem);

void print_out(u32 address, u32 pc)
{
  char buffer[256];
  sprintf(buffer, "patching from gp8 %x", address);
  print_string(buffer, 0xFFFF, 0x0000, 0, 0);
  update_screen();
  delay_us(5000000);
}
