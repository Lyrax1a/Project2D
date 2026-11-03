`timescale 1ns/1ps
// SPDX-License-Identifier: MIT
// APB sub-bank 0x200..0x23f. Caller must exclude it from the GPU CSR bank.
module ts2d_audio_apb #(parameter PIXEL_HZ=148750000)(
 input wire clk,rst,pixel_clk,pixel_rst,
 input wire [15:0] paddr,input wire psel,penable,pwrite,input wire [31:0] pwdata,
 output reg [31:0] prdata,
 output wire [23:0] audio_l,audio_r,output wire audio_valid
);
 wire selected=psel&&paddr[15:6]==10'h008;
 wire write_enable=selected&&penable&&pwrite;
 wire ready;wire [31:0] dropped,samples;
 wire audio_arst=rst|pixel_rst;
 (* ASYNC_REG="TRUE" *) reg [2:0] user_reset_pipe,pixel_reset_pipe;
 always @(posedge clk or posedge audio_arst)
   if(audio_arst)user_reset_pipe<=3'b111;else user_reset_pipe<={user_reset_pipe[1:0],1'b0};
 always @(posedge pixel_clk or posedge audio_arst)
   if(audio_arst)pixel_reset_pipe<=3'b111;else pixel_reset_pipe<={pixel_reset_pipe[1:0],1'b0};
 ts2d_audio_synth #(.PIXEL_HZ(PIXEL_HZ)) synth(
   .user_clk(clk),.user_rst(user_reset_pipe[2]),.pixel_clk(pixel_clk),.pixel_rst(pixel_reset_pipe[2]),
   .event_valid(write_enable&&paddr[5:2]==4'h2),.event_data(pwdata[15:0]),
   .event_ready(ready),.dropped(dropped),
   .control_write(write_enable&&paddr[5:2]==4'h3),.control_data(pwdata[1:0]),
   .sample_count(samples),.audio_l(audio_l),.audio_r(audio_r),.audio_valid(audio_valid));
 always @* begin
   prdata=0;
   case(paddr[5:2])
     0:prdata=32'h41554431;
     1:prdata={31'b0,ready};
     4:prdata=dropped;
     5:prdata=samples;
     default:prdata=0;
   endcase
 end
endmodule
