`timescale 1ns/1ps
// RGB565 DDR scanout: 1024x576 -> 1920x1080 with RGB888 bilinear interpolation.
// Four 128-bit line banks are written in the memory domain and read in pixel
// domain. A toggle mailbox transfers stable {base,row,bank}, and the reverse
// mailbox acknowledges after the final successful beat. Never display a line
// that missed its deadline; count it and output black for the complete pair.
module ts2d_scanout(
 input wire clk,rst,pixel_clk,pixel_rst,
 input wire [15:0] paddr,input wire psel,penable,pwrite,input wire [31:0] pwdata,
 output reg [31:0] prdata,
 output wire [31:0] araddr,output wire [7:0] arlen,
 output wire [2:0] arsize,output wire [1:0] arburst,
 output wire arvalid,input wire arready,
 input wire [127:0] rdata,input wire [1:0] rresp,
 input wire rlast,rvalid,output wire rready,
 input wire osd_lit,osd_area,
 output wire [11:0] pixel_x,output wire [10:0] pixel_y,
 output reg hs,vs,de,output reg [7:0] red,green,blue
);
 localparam FB_A=32'h01000000,FB_B=32'h01400000;
 reg [31:0] pending_base;
 reg swap_request;
 (* async_reg="true" *) reg ack1,ack2;
 reg swap_ack;
 reg [31:0] presented;
 reg ack_old;
 reg filter_enable;
 wire wr=psel && penable && pwrite;
 wire swap_busy=swap_request!=ack2;
 reg [31:0] errors;
 reg [31:0] line_misses,miss_gray;
 (* async_reg="true" *) reg [31:0] mg1,mg2;
 reg [31:0] miss_cpu;
 integer gi;
 always @* begin
  miss_cpu[31]=mg2[31];
  for(gi=30;gi>=0;gi=gi-1)miss_cpu[gi]=miss_cpu[gi+1]^mg2[gi];
  prdata=0;
  case(paddr)
   16'h400:prdata=32'h44535032;
   16'h404:prdata={31'b0,swap_busy};
   16'h408:prdata=pending_base;
   16'h410:prdata=presented;
   16'h414:prdata=miss_cpu;
   16'h418:prdata=errors;
   16'h41c:prdata={31'b0,filter_enable};
  endcase
 end
 always @(posedge clk)begin
  if(rst)begin pending_base<=FB_A;swap_request<=0;ack1<=0;ack2<=0;ack_old<=0;presented<=0;mg1<=0;mg2<=0;filter_enable<=1;end
  else begin
   ack1<=swap_ack;ack2<=ack1;ack_old<=ack2;
   mg1<=miss_gray;mg2<=mg1;
   if(ack2!=ack_old)presented<=presented+1'b1;
   if(wr && paddr==16'h408 && !swap_busy && (pwdata==FB_A || pwdata==FB_B))pending_base<=pwdata;
   if(wr && paddr==16'h40c && !swap_busy)swap_request<=~swap_request;
   if(wr && paddr==16'h41c)filter_enable<=pwdata[0];
  end
 end

 reg request,ack;
 reg [31:0] request_base;
 reg [9:0] request_row;
 reg [1:0] request_bank;
 (* async_reg="true" *) reg rq1,rq2;
 reg ack_ok;
 reg [9:0] ack_row;
 reg [1:0] ack_bank;
 reg [1:0] state;
   reg [7:0] beat;
 reg [3:0] burst_beat;
 reg failed;
 reg [31:0] address;
 reg [1:0] bank;
 reg [9:0] row;
 assign araddr=address;
 // 128-byte bursts divide the 2048-byte stride and never cross 4 KiB.
 assign arlen=7;
 assign arsize=4;
 assign arburst=1;
 assign arvalid=state==1;
 assign rready=state==2;
 always @(posedge clk)begin
  if(rst)begin rq1<=0;rq2<=0;ack<=0;ack_ok<=0;ack_row<=0;ack_bank<=0;
   state<=0;beat<=0;burst_beat<=0;failed<=0;address<=0;bank<=0;row<=0;errors<=0;
  end else begin
   rq1<=request;rq2<=rq1;
   case(state)
    0:if(rq2!=ack)begin
     // Bundled-data request has been stable for two receiving clocks.
      address<=request_base+request_row*2048;bank<=request_bank;row<=request_row;
     beat<=0;burst_beat<=0;failed<=0;state<=1;
    end
    1:if(arready)state<=2;
    2:if(rvalid)begin
     if(rresp!=0 || rlast!=(burst_beat==7))begin failed<=1;errors<=errors+1'b1;end
     if(burst_beat==7)begin
      burst_beat<=0;
     if(beat==127)begin
       ack<=rq2;ack_row<=row;ack_bank<=bank;ack_ok<=!failed && rresp==0 && rlast;
       state<=0;
      end else begin address<=address+128;state<=1;end
     end else burst_beat<=burst_beat+1'b1;
     beat<=beat+1'b1;
    end
   endcase
  end
 end

 reg [11:0] h;
 reg [10:0] v;
 reg [31:0] front_base;
 reg enabled;
 assign pixel_x=h;assign pixel_y=v;
 reg osd_lit_q,osd_area_q,osd_lit_q2,osd_area_q2;
 (* async_reg="true" *) reg sw1,sw2,la1,la2;
 (* async_reg="true" *) reg filter1,filter2;
 reg filter_frame;
 reg la_old;
 reg [9:0] tags[0:3];
 reg [3:0] line_ready;
 reg line_good;
 reg preload_second;
 wire picture=h<1920 && v<1080;
 wire [9:0] source_y=(v*32'd8)/15;
 wire [10:0] next_v=(v==1124)?11'd0:v+1'b1;
 wire [9:0] next_source_y=(next_v*32'd8)/15;
 wire [9:0] source_x=(h*32'd8)/15;
 wire [9:0] right_x=source_x==1023?source_x:source_x+1'b1;
 wire [9:0] below_y=source_y==575?source_y:source_y+1'b1;
 wire [9:0] next_below_y=next_source_y==575?next_source_y:next_source_y+1'b1;
 wire next_lines_ready=line_ready[next_source_y[1:0]] && tags[next_source_y[1:0]]==next_source_y &&
                       line_ready[next_below_y[1:0]] && tags[next_below_y[1:0]]==next_below_y;
 wire [63:0] even_words[0:3],odd_words[0:3];
 // Adjacent pixels always have opposite parity. Split each input beat into
 // even/odd pixels, providing independent reads without duplicating storage.
 genvar g;
 generate for(g=0;g<2;g=g+1)begin: line_bank
  // Adjacent source rows have opposite parity. Fold the four logical lines
  // into two deeper banks while keeping independent top/bottom pixel reads.
  (* ram_style="block" *) reg [63:0] even_ram[0:255],odd_ram[0:255];
  reg [63:0] even_q,odd_q;
  wire read_half=source_y[0]==g ? source_y[1] : below_y[1];
  always @(posedge clk)if(!rst && state==2 && rvalid && bank[0]==g)begin
   even_ram[(bank[1]?128:0)+beat]<={rdata[111:96],rdata[79:64],rdata[47:32],rdata[15:0]};
   odd_ram[(bank[1]?128:0)+beat]<={rdata[127:112],rdata[95:80],rdata[63:48],rdata[31:16]};
  end
  always @(posedge pixel_clk)if(picture)begin
   even_q<=even_ram[(read_half?128:0)+(right_x>>3)];
   odd_q<=odd_ram[(read_half?128:0)+(source_x>>3)];
  end
  assign even_words[g]=even_q;assign even_words[g+2]=even_q;
  assign odd_words[g]=odd_q;assign odd_words[g+2]=odd_q;
 end endgenerate
 reg [2:0] lane_q,right_lane_q;
 reg [1:0] top_bank_q,bottom_bank_q;
 reg fx_q,fy_q,fx_q2,fy_q2;
 reg de_q,hs_q,vs_q,picture_q,de_q2,hs_q2,vs_q2,picture_q2;
 reg [15:0] p00,p10,p01,p11;
 function [7:0] expand5(input [4:0] c);begin expand5={c,c[4:2]};end endfunction
 function [7:0] expand6(input [5:0] c);begin expand6={c,c[5:4]};end endfunction
 function [7:0] interpolate(input [7:0] a,b,c,d,input fx,fy);
  reg [9:0] sum;
  begin
   case({fy,fx})
    0:interpolate=a;
    1:begin sum={2'b0,a}+{2'b0,b}+1;interpolate=sum[8:1];end
    2:begin sum={2'b0,a}+{2'b0,c}+1;interpolate=sum[8:1];end
    3:begin sum={2'b0,a}+{2'b0,b}+{2'b0,c}+{2'b0,d}+2;interpolate=sum[9:2];end
   endcase
  end
 endfunction
 integer ti;
 always @(posedge pixel_clk)begin
  if(pixel_rst)begin
   h<=0;v<=0;front_base<=FB_A;enabled<=0;swap_ack<=0;sw1<=0;sw2<=0;
   request<=0;request_base<=FB_A;request_row<=0;request_bank<=0;
   la1<=0;la2<=0;la_old<=0;line_ready<=0;preload_second<=0;
   for(ti=0;ti<4;ti=ti+1)tags[ti]<=0;
   filter1<=1;filter2<=1;filter_frame<=1;
   line_good<=0;line_misses<=0;miss_gray<=0;
   hs<=0;vs<=0;de<=0;red<=0;green<=0;blue<=0;
   osd_lit_q<=0;osd_area_q<=0;osd_lit_q2<=0;osd_area_q2<=0;
   de_q<=0;hs_q<=0;vs_q<=0;picture_q<=0;de_q2<=0;hs_q2<=0;vs_q2<=0;picture_q2<=0;
   lane_q<=0;right_lane_q<=0;top_bank_q<=0;bottom_bank_q<=0;
   fx_q<=0;fy_q<=0;fx_q2<=0;fy_q2<=0;p00<=0;p10<=0;p01<=0;p11<=0;
  end else begin
   sw1<=swap_request;sw2<=sw1;la1<=ack;la2<=la1;la_old<=la2;
   filter1<=filter_enable;filter2<=filter1;
   if(la2!=la_old)begin
    line_ready[ack_bank]<=ack_ok;tags[ack_bank]<=ack_row;
   end
   if(h==2199)begin h<=0;if(v==1124)v<=0;else v<=v+1'b1;end
   else h<=h+1'b1;
   // Commit only in vertical blank, with the preceding DMA fully drained.
   if(h==0 && v==1100 && request==la2)begin
    if(sw2!=swap_ack)begin front_base<=pending_base;swap_ack<=sw2;enabled<=1;end
    line_ready<=0;filter_frame<=filter2;
   end
   if(h==0 && v==1110 && request==la2 && enabled)begin
    request_base<=front_base;request_row<=0;request_bank<=0;request<=~request;
    preload_second<=1;
   end
   if(preload_second && v>=1110 && request==la2)begin
    request_base<=front_base;request_row<=1;request_bank<=1;request<=~request;
    preload_second<=0;
   end
   // Decide before the first active pixel, keeping validity for both repeats.
    if(h==2198 && next_v<1080)begin
    line_good<=enabled && next_lines_ready;
    if(enabled && !next_lines_ready)begin
     line_misses<=line_misses+1'b1;miss_gray<=(line_misses+1'b1)^((line_misses+1'b1)>>1);
    end
     if(request==la2 && next_source_y<574 && enabled && next_source_y != source_y)begin
      request_base<=front_base;request_row<=next_source_y+2;
      request_bank<=next_source_y[1:0]+2'd2;request<=~request;
    end
   end
   // RAM read -> four pixel selection -> interpolation and RGB output.
   top_bank_q<=source_y[1:0];bottom_bank_q<=below_y[1:0];right_lane_q<=right_x[2:0];
   lane_q<=source_x[2:0];picture_q<=picture && line_good;
    fx_q<=(((h*32'd8)-source_x*15)>=8) && filter_frame;
    fy_q<=(((v*32'd8)-source_y*15)>=8) && filter_frame;
   p00<=lane_q[0]?odd_words[top_bank_q][lane_q[2:1]*16+:16]:even_words[top_bank_q][lane_q[2:1]*16+:16];
   p10<=right_lane_q[0]?odd_words[top_bank_q][right_lane_q[2:1]*16+:16]:even_words[top_bank_q][right_lane_q[2:1]*16+:16];
   p01<=lane_q[0]?odd_words[bottom_bank_q][lane_q[2:1]*16+:16]:even_words[bottom_bank_q][lane_q[2:1]*16+:16];
   p11<=right_lane_q[0]?odd_words[bottom_bank_q][right_lane_q[2:1]*16+:16]:even_words[bottom_bank_q][right_lane_q[2:1]*16+:16];
   fx_q2<=fx_q;fy_q2<=fy_q;picture_q2<=picture_q;
   de_q<=h<1920 && v<1080;hs_q<=h>=2008 && h<2052;vs_q<=v>=1084 && v<1089;
   de_q2<=de_q;hs_q2<=hs_q;vs_q2<=vs_q;
   de<=de_q2;hs<=hs_q2;vs<=vs_q2;
   osd_lit_q<=osd_lit;osd_area_q<=osd_area;
   osd_lit_q2<=osd_lit_q;osd_area_q2<=osd_area_q;
   if(osd_area_q2)begin
    red<=osd_lit_q2?8'd220:8'd3;green<=osd_lit_q2?8'd235:8'd5;blue<=osd_lit_q2?8'd255:8'd12;
   end else if(picture_q2)begin
    red<=interpolate(expand5(p00[15:11]),expand5(p10[15:11]),expand5(p01[15:11]),expand5(p11[15:11]),fx_q2,fy_q2);
    green<=interpolate(expand6(p00[10:5]),expand6(p10[10:5]),expand6(p01[10:5]),expand6(p11[10:5]),fx_q2,fy_q2);
    blue<=interpolate(expand5(p00[4:0]),expand5(p10[4:0]),expand5(p01[4:0]),expand5(p11[4:0]),fx_q2,fy_q2);
   end else begin red<=0;green<=0;blue<=0;end
  end
 end
endmodule
