`timescale 1ns/1ps
// SPDX-License-Identifier: MIT
// Independent 48 kHz stereo tone source; no video timing or DDR accesses.
// User domain: EVENT={gain[7:0],id[7:0]}, control bit0=mute bit1=clear.
// The 16-entry Gray-pointer FIFO is the only event clock-domain crossing.
module ts2d_audio_synth #(parameter PIXEL_HZ=148750000)(
 input wire user_clk,user_rst,pixel_clk,pixel_rst,
 input wire event_valid,input wire [15:0] event_data,
 output wire event_ready,output reg [31:0] dropped,
 input wire control_write,input wire [1:0] control_data,
 output wire [31:0] sample_count,
 output reg [23:0] audio_l,audio_r,output reg audio_valid
);
 reg [15:0] fifo[0:15];
 reg [4:0] wp,wgray,rp,rgray;
 (* ASYNC_REG="TRUE" *) reg [4:0] rgray_u1,rgray_u2,wgray_p1,wgray_p2;
 wire [4:0] wp_next=wp+1'b1;
 wire [4:0] wgray_next=(wp_next>>1)^wp_next;
 wire fifo_full=(wgray=={~rgray_u2[4:3],rgray_u2[2:0]});
 wire fifo_empty=(rgray==wgray_p2);
 assign event_ready=!fifo_full;
 reg control_mute,control_clear;
 (* ASYNC_REG="TRUE" *) reg mute_p1,mute_p2,clear_p1,clear_p2;
 reg clear_seen,discard_pending;
 reg [31:0] samples,samples_gray;
 (* ASYNC_REG="TRUE" *) reg [31:0] samples_u1,samples_u2;
 function [31:0] gray_binary;input [31:0] x;integer k;begin
   gray_binary[31]=x[31];for(k=30;k>=0;k=k-1)gray_binary[k]=gray_binary[k+1]^x[k];
 end endfunction
 assign sample_count=gray_binary(samples_u2);
 always @(posedge user_clk)begin
   if(user_rst)begin wp<=0;wgray<=0;rgray_u1<=0;rgray_u2<=0;dropped<=0;
     control_mute<=0;control_clear<=0;samples_u1<=0;samples_u2<=0;end
   else begin
     rgray_u1<=rgray;rgray_u2<=rgray_u1;samples_u1<=samples_gray;samples_u2<=samples_u1;
     if(control_write)begin control_mute<=control_data[0];if(control_data[1])control_clear<=~control_clear;end
     if(event_valid)begin
       if(!fifo_full)begin fifo[wp[3:0]]<=event_data;wp<=wp_next;wgray<=wgray_next;end
       else dropped<=dropped+1'b1;
     end
   end
 end
 reg [31:0] sample_acc;
 reg [23:0] phase[0:3],increment[0:3];
 reg [15:0] remaining[0:3];
 reg [7:0] gain[0:3];
 reg [1:0] next_voice;
 reg [15:0] event_word;
 integer voice,j;
 reg signed [16:0] triangle;
 reg signed [26:0] scaled;
 reg signed [31:0] mixed;
 reg signed [35:0] faded;
 reg [7:0] fade;
 reg [7:0] mix_gain;
 reg [2:0] mix_state;
 reg [1:0] mix_voice;
 function [23:0] pitch;input [7:0] id;begin
   case(id)
     1:pitch=24'd307582;2:pitch=24'd447392;3:pitch=24'd97867;
     4:pitch=24'd55924;5:pitch=24'd41943;6:pitch=24'd230687;
     7:pitch=24'd153791;8:pitch=24'd366477;9:pitch=24'd183238;
     10:pitch=24'd73295;11:pitch=24'd279620;
     16:pitch=24'd91457;17:pitch=24'd102654;18:pitch=24'd115341;19:pitch=24'd122191;
     20:pitch=24'd137158;21:pitch=24'd153791;22:pitch=24'd172741;23:pitch=24'd183238;
     default:pitch=0;
   endcase
 end endfunction
 function [15:0] duration;input [7:0] id;begin
   case(id)
     1:duration=2400;2:duration=1440;3:duration=1920;4:duration=4800;
     5:duration=9600;6:duration=7200;7:duration=4800;8:duration=9600;
     9:duration=24000;10:duration=24000;11:duration=12000;
     16,17,18,19,20,21,22,23:duration=16000;
     default:duration=0;
   endcase
 end endfunction
 always @(posedge pixel_clk)begin
   if(pixel_rst)begin
     rp<=0;rgray<=0;wgray_p1<=0;wgray_p2<=0;mute_p1<=0;mute_p2<=0;
     clear_p1<=0;clear_p2<=0;clear_seen<=0;discard_pending<=0;sample_acc<=0;samples<=0;samples_gray<=0;
     audio_l<=0;audio_r<=0;audio_valid<=0;next_voice<=0;mix_state<=0;mix_voice<=0;
     triangle<=0;scaled<=0;faded<=0;mixed<=0;fade<=0;mix_gain<=0;
     for(j=0;j<4;j=j+1)begin phase[j]<=0;increment[j]<=0;remaining[j]<=0;gain[j]<=0;end
   end else begin
     wgray_p1<=wgray;wgray_p2<=wgray_p1;mute_p1<=control_mute;mute_p2<=mute_p1;
     clear_p1<=control_clear;clear_p2<=clear_p1;audio_valid<=0;
     if(sample_acc>=PIXEL_HZ-48000)begin
       sample_acc<=sample_acc+48000-PIXEL_HZ;
       mixed<=0;mix_voice<=0;mix_state<=1;
     end else sample_acc<=sample_acc+48000;
     // Four serial voices, one multiply per pipeline stage. All 17 cycles fit
     // comfortably in the 3098/3099-pixel-clock sample interval at 148.75 MHz.
     case(mix_state)
       1:begin
         triangle<=phase[mix_voice][23]?17'sd32767-$signed({1'b0,phase[mix_voice][22:7]}):$signed({1'b0,phase[mix_voice][22:7]})-17'sd32768;
         fade<=remaining[mix_voice]>4095?8'd255:remaining[mix_voice][11:4];
         mix_gain<=remaining[mix_voice]?gain[mix_voice]:0;
         if(remaining[mix_voice])begin phase[mix_voice]<=phase[mix_voice]+increment[mix_voice];remaining[mix_voice]<=remaining[mix_voice]-1'b1;end
         mix_state<=2;
       end
       2:begin scaled<=triangle*$signed({1'b0,mix_gain});mix_state<=3;end
       3:begin faded<=$signed(scaled>>>2)*$signed({1'b0,fade});mix_state<=4;end
       4:begin
         mixed<=mixed+(faded>>>8);
         if(mix_voice==3)mix_state<=5;
         else begin mix_voice<=mix_voice+1'b1;mix_state<=1;end
       end
       5:begin
         if(mute_p2)begin audio_l<=0;audio_r<=0;end
         else begin audio_l<=mixed[23:0];audio_r<=mixed[23:0];end
         audio_valid<=1;samples<=samples+1'b1;samples_gray<=((samples+1'b1)>>1)^(samples+1'b1);mix_state<=0;
       end
       default:begin end
     endcase
     // Commands are drained independently of sample timing, max one per pixel.
     if(!fifo_empty)begin
       event_word=fifo[rp[3:0]];rp<=rp+1'b1;rgray<=((rp+1'b1)>>1)^(rp+1'b1);
       if(event_word[7:0]>=16)voice=3;
       else begin voice=next_voice;next_voice<=next_voice==2?0:next_voice+1'b1;end
       if(!mute_p2&&!discard_pending)begin phase[voice]<=0;increment[voice]<=pitch(event_word[7:0]);
         remaining[voice]<=duration(event_word[7:0]);gain[voice]<=event_word[15:8];end
     end else discard_pending<=0;
     if(clear_p2!=clear_seen)begin
       clear_seen<=clear_p2;discard_pending<=1;
       for(j=0;j<4;j=j+1)remaining[j]<=0;
       audio_l<=0;audio_r<=0;mix_state<=0;
     end
   end
 end
endmodule
