`timescale 1ns/1ps
// Limited integer implementation of the original binary32 Alpha expression.
// Standalone experiment: not connected to the ordinary renderer/bitstream.
// Accepted subset: 0 <= numerator <= area <= 2^24; paint=0 or [2^-32,1].
// One input on valid_i&&ready_o; output valid pulses once, in order. Reset
// cancels an in-flight request. No input is accepted while ready_o is low.
module ts2d_triangle_alpha_exact (
    input wire clk, rst_n, valid_i,
    input wire [31:0] numerator_i, area_i, paint_i,
    output wire ready_o,
    output reg valid_o, error_o,
    output reg [7:0] alpha_o
);
    localparam [3:0] IDLE=0,NORMALIZE=1,DIVIDE=2,RATIO_ROUND=3,
        MUL_START=4,MUL_ROUND=5,ADD_ALIGN=6,ADD_SUM=7,ADD_ROUND=8,CONVERT=9;
    reg [3:0] state_q;
    reg [31:0] area_q, paint_q, remainder_q, mantissa_q;
    reg [7:0] exponent_q;
    reg [4:0] bit_q;
    reg [31:0] operand_a_q,operand_b_q,value_q;
    reg [47:0] product_q;
    reg [9:0] product_exponent_q;
    reg multiply_255_q;
    reg [31:0] add_big_q,add_small_q,add_sum_q;
    reg [7:0] add_exponent_q;
    assign ready_o=state_q==IDLE;

    wire [32:0] doubled_remainder={1'b0,remainder_q}<<1;
    wire ratio_up=doubled_remainder>{1'b0,area_q} ||
        (doubled_remainder=={1'b0,area_q} && mantissa_q[0]);
    wire [24:0] rounded_ratio={1'b0,mantissa_q[23:0]}+ratio_up;
    wire [7:0] ratio_exponent=exponent_q+rounded_ratio[24];
    wire [22:0] ratio_fraction=rounded_ratio[24] ? rounded_ratio[23:1] : rounded_ratio[22:0];

    wire product_wide=product_q[47];
    wire [23:0] product_mantissa=product_wide ? product_q[47:24] : product_q[46:23];
    wire product_up=product_wide ?
        (product_q[23] && ((|product_q[22:0]) || product_mantissa[0])) :
        (product_q[22] && ((|product_q[21:0]) || product_mantissa[0]));
    wire [24:0] rounded_product={1'b0,product_mantissa}+product_up;
    wire [9:0] multiplied_exponent=product_exponent_q+product_wide+rounded_product[24];
    wire [22:0] multiplied_fraction=rounded_product[24] ? rounded_product[23:1] : rounded_product[22:0];
    wire [31:0] multiplied_bits={1'b0,multiplied_exponent[7:0],multiplied_fraction};

    reg [31:0] align_a,align_b,align_small;
    reg [7:0] align_shift;
    always @* begin
        if(value_q<32'h3f000000)begin align_a=32'h3f000000;align_b=value_q;end
        else begin align_a=value_q;align_b=32'h3f000000;end
        align_shift=align_a[30:23]-align_b[30:23];
        align_small={5'b0,1'b1,align_b[22:0],3'b0};
        if(align_shift>=32)align_small=1;
        else if(align_shift!=0)align_small=(align_small>>align_shift) |
            ((align_small & ((32'h1<<align_shift)-1))!=0);
    end
    wire [31:0] normalized_add=add_sum_q[27] ?
        ((add_sum_q>>1)|(add_sum_q&1)) : add_sum_q;
    wire add_up=normalized_add[2] && ((|normalized_add[1:0]) || normalized_add[3]);
    wire [24:0] rounded_add={1'b0,normalized_add[26:3]}+add_up;
    wire [7:0] final_add_exponent=add_exponent_q+add_sum_q[27]+rounded_add[24];
    wire [22:0] final_add_fraction=rounded_add[24] ? rounded_add[23:1] : rounded_add[22:0];

    always @(posedge clk or negedge rst_n)begin
        if(!rst_n)begin
            state_q<=IDLE;valid_o<=0;error_o<=0;alpha_o<=0;
        end else begin
            valid_o<=0;
            case(state_q)
            IDLE:if(valid_i)begin
                error_o<=0;alpha_o<=0;
                if(area_i==0 || area_i>32'h01000000 || numerator_i>area_i ||
                   (paint_i!=0 && (paint_i<32'h2f800000 || paint_i>32'h3f800000)))begin
                    error_o<=1;valid_o<=1;
                end else if(numerator_i==0 || paint_i==0)valid_o<=1;
                else begin
                    area_q<=area_i;paint_q<=paint_i;remainder_q<=numerator_i;
                    exponent_q<=127;mantissa_q<=32'h00800000;state_q<=NORMALIZE;
                end
            end
            NORMALIZE:begin
                if(remainder_q<area_q)begin remainder_q<=remainder_q<<1;exponent_q<=exponent_q-1'b1;end
                else begin remainder_q<=remainder_q-area_q;bit_q<=22;state_q<=DIVIDE;end
            end
            DIVIDE:begin
                if(doubled_remainder>={1'b0,area_q})begin
                    remainder_q<=doubled_remainder-area_q;mantissa_q[bit_q]<=1;
                end else remainder_q<=doubled_remainder[31:0];
                if(bit_q==0)state_q<=RATIO_ROUND;else bit_q<=bit_q-1'b1;
            end
            RATIO_ROUND:begin
                operand_a_q<={1'b0,ratio_exponent,ratio_fraction};operand_b_q<=paint_q;
                multiply_255_q<=0;state_q<=MUL_START;
            end
            MUL_START:begin
                product_q<={1'b1,operand_a_q[22:0]}*{1'b1,operand_b_q[22:0]};
                product_exponent_q<={2'b0,operand_a_q[30:23]}+{2'b0,operand_b_q[30:23]}-10'd127;
                state_q<=MUL_ROUND;
            end
            MUL_ROUND:begin
                if(!multiply_255_q)begin
                    if(multiplied_bits>=32'h3f800000)begin alpha_o<=255;valid_o<=1;state_q<=IDLE;end
                    else begin operand_a_q<=multiplied_bits;operand_b_q<=32'h437f0000;multiply_255_q<=1;state_q<=MUL_START;end
                end else begin value_q<=multiplied_bits;state_q<=ADD_ALIGN;end
            end
            ADD_ALIGN:begin
                add_big_q<={5'b0,1'b1,align_a[22:0],3'b0};add_small_q<=align_small;
                add_exponent_q<=align_a[30:23];state_q<=ADD_SUM;
            end
            ADD_SUM:begin add_sum_q<=add_big_q+add_small_q;state_q<=ADD_ROUND;end
            ADD_ROUND:begin value_q<={1'b0,final_add_exponent,final_add_fraction};state_q<=CONVERT;end
            CONVERT:begin
                alpha_o<=value_q[30:23]<127 ? 8'd0 :
                    ({1'b1,value_q[22:0]}>>(8'd150-value_q[30:23]));
                valid_o<=1;state_q<=IDLE;
            end
            default:begin state_q<=IDLE;error_o<=1;alpha_o<=0;valid_o<=1;end
            endcase
        end
    end
endmodule
