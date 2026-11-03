`timescale 1ns/1ps
// 4 rows x 64 characters. The host writes a shadow page, commits once,
// and waits for the toggle acknowledgement before changing it. The pixel
// domain copies the stable page in vertical blank. No multi-bit live CDC.
module ts2d_osd(
 input wire clk,rst,pixel_clk,pixel_rst,
 input wire [15:0] paddr,input wire psel,penable,pwrite,input wire [31:0] pwdata,
 output wire [31:0] prdata,
 input wire [11:0] h,input wire [10:0] v,output wire lit,output wire area
);
reg [7:0] shadow[0:255];reg [7:0] front[0:255];
reg request,ack;
(* async_reg="true" *) reg a1,a2,r1,r2;
assign prdata={31'b0,request!=a2};
integer i;
always @(posedge clk)begin
 if(rst)begin request<=0;a1<=0;a2<=0;end
 else begin
  a1<=ack;a2<=a1;
  if(psel && penable && pwrite && request==a2)begin
   if(paddr>=16'h800 && paddr<16'hc00)shadow[paddr[9:2]]<=pwdata[7:0];
   if(paddr==16'h700)request<=~request;
  end
 end
end
reg valid;
always @(posedge pixel_clk)begin
 if(pixel_rst)begin r1<=0;r2<=0;ack<=0;valid<=0;end
 else begin
  r1<=request;r2<=r1;
  if(h==0 && v==1100 && r2!=ack)begin
   for(i=0;i<256;i=i+1)front[i]<=shadow[i];
   ack<=r2;valid<=1;
  end
 end
end
function [34:0] glyph(input [7:0] ch);
begin case(ch)
8'd65:glyph=35'b01110100011000111111100011000110001;
8'd66:glyph=35'b11110100011000111110100011000111110;
8'd67:glyph=35'b01111100001000010000100001000001111;
8'd68:glyph=35'b11110100011000110001100011000111110;
8'd69:glyph=35'b11111100001000011110100001000011111;
8'd70:glyph=35'b11111100001000011110100001000010000;
8'd71:glyph=35'b01111100001000010111100011000101111;
8'd72:glyph=35'b10001100011000111111100011000110001;
8'd73:glyph=35'b11111001000010000100001000010011111;
8'd74:glyph=35'b00111000100001000010100101001001100;
8'd75:glyph=35'b10001100101010011000101001001010001;
8'd76:glyph=35'b10000100001000010000100001000011111;
8'd77:glyph=35'b10001110111010110101100011000110001;
8'd78:glyph=35'b10001110011010110011100011000110001;
8'd79:glyph=35'b01110100011000110001100011000101110;
8'd80:glyph=35'b11110100011000111110100001000010000;
8'd81:glyph=35'b01110100011000110001101011001001101;
8'd82:glyph=35'b11110100011000111110101001001010001;
8'd83:glyph=35'b01111100001000001110000010000111110;
8'd84:glyph=35'b11111001000010000100001000010000100;
8'd85:glyph=35'b10001100011000110001100011000101110;
8'd86:glyph=35'b10001100011000110001100010101000100;
8'd87:glyph=35'b10001100011000110101101011010101010;
8'd88:glyph=35'b10001100010101000100010101000110001;
8'd89:glyph=35'b10001100010101000100001000010000100;
8'd90:glyph=35'b11111000010001000100010001000011111;
8'd48:glyph=35'b01110100011001110101110011000101110;
8'd49:glyph=35'b00100011000010000100001000010001110;
8'd50:glyph=35'b01110100010000100010001000100011111;
8'd51:glyph=35'b11110000010000101110000010000111110;
8'd52:glyph=35'b00010001100101010010111110001000010;
8'd53:glyph=35'b11111100001000011110000010000111110;
8'd54:glyph=35'b01110100001000011110100011000101110;
8'd55:glyph=35'b11111000010001000100010000100001000;
8'd56:glyph=35'b01110100011000101110100011000101110;
8'd57:glyph=35'b01110100011000101111000010000101110;
8'd45:glyph=35'b00000000000000011111000000000000000;
8'd58:glyph=35'b00000001000010000000001000010000000;
8'd47:glyph=35'b00001000100001000100010000100010000;
8'd46:glyph=35'b00000000000000000000000000011000110;
8'd43:glyph=35'b00000001000010011111001000010000000;
default:glyph=0;
endcase end endfunction
wire [11:0] dx=h-192;
wire [10:0] dy=v-8;
wire [5:0] col=dx/24;
wire [1:0] row=dy/32;
wire [2:0] gx=(dx%24)/4;
wire [2:0] gy=(dy%32)/4;
wire [7:0] current_char=front[{row,col}];
wire [34:0] bits=glyph(current_char);
/* Do not paint the OSD backdrop for blank cells. This keeps a committed
 * empty page transparent and prevents the historical top-screen black bar. */
assign area=valid && current_char!=8'd0 && current_char!=8'd32 &&
            h>=192 && h<1728 && v>=8 && v<136;
assign lit=area && gx<5 && gy<7 && bits[34-gy*5-gx];
endmodule
