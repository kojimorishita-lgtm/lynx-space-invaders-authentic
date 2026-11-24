; ---------------------------------------------------------------------------
; sndirq.asm -- v50 sample-playback engine for the Lynx port.
;
; One Mikey audio channel (A) is used as a plain DAC: its shifter is
; held at all-ones with NO feedback taps (the LFSR input bit is then a
; constant 1) while the channel timer runs at a 1 us tick, so the
; channel output value continuously follows the signed volume register.
; (The output value only updates while the channel timer counts, so
; the timer must run; a stopped channel stays silent.)  Timer 7 (the
; designated sound timer) raises an IRQ at the sample rate and the
; interruptor below feeds one sample per tick.
;
; Timer 7 always uses BKUP = 90; the clock select picks the rate:
;   1 (2 us tick) ->  5495 Hz   (shoot, killed, explosion: IMA-ADPCM;
;                                march notes, ufo: raw)
;   2 (4 us tick) ->  2747 Hz   (extend, raw: a near-pure 480 Hz tone)
; Clock select 0 (10989 Hz) is NOT used: the ADPCM decode is long
; enough that at a 91 us period every other tick is lost in practice
; (measured; a raw feed at the same rate was exact).
;
; Priorities (higher preempts): explosion 60, extend 50, killed 40,
; shoot 30, ufo loop 20, march notes 10.  A request with lower priority
; than the sound currently playing is dropped; equal priority restarts.
; ---------------------------------------------------------------------------

        .include        "lynx.inc"

        .import         _snd_data_shoot, _snd_data_killed
        .import         _snd_data_explosion, _snd_data_extend
        .import         _snd_data_march1, _snd_data_march2
        .import         _snd_data_march3, _snd_data_march4
        .import         _snd_data_ufo, _snd_data_ufohit
        .import         _snd_vpdiff_tab

        .export         _snd_init, _snd_play, _snd_stop
        .export         _snd_active, _snd_cur

        .interruptor    snd_irq, 10

; --- hot pointer in application zeropage (needed for (zp),y) ---------------
        .segment        "APPZP" : zeropage
snd_ptr:        .res    2
vp_ptr:         .res    2

; --- state -------------------------------------------------------------------
        .bss
_snd_active:    .res    1
_snd_cur:       .res    1
snd_cur_prio:   .res    1
snd_start:      .res    2
snd_len:        .res    2       ; in samples (raw: = bytes)
snd_rem:        .res    2
snd_loop:       .res    1
snd_adpcm_cur:  .res    1
snd_nibsave:    .res    1
snd_old_index:  .res    1
snd_pred:       .res    2       ; ADPCM predictor (signed 16-bit)
snd_index:      .res    1       ; ADPCM step index 0..88
snd_nibflag:    .res    1       ; 0 = low nibble next, 1 = high nibble next
snd_nibbyte:    .res    1
snd_step:       .res    2
snd_vp:         .res    2
snd_tmp:        .res    2

; --- tables (index = sound id; id 0 unused) ----------------------------------
; ids: 1 shoot, 2 killed, 3 explosion, 4 extend, 5..8 march1..4, 9 ufo loop, 10 ufohit
        .rodata
snd_ptr_lo:     .byte   0
                .byte   <_snd_data_shoot, <_snd_data_killed
                .byte   <_snd_data_explosion, <_snd_data_extend
                .byte   <_snd_data_march1, <_snd_data_march2
                .byte   <_snd_data_march3, <_snd_data_march4
                .byte   <_snd_data_ufo, <_snd_data_ufohit
snd_ptr_hi:     .byte   0
                .byte   >_snd_data_shoot, >_snd_data_killed
                .byte   >_snd_data_explosion, >_snd_data_extend
                .byte   >_snd_data_march1, >_snd_data_march2
                .byte   >_snd_data_march3, >_snd_data_march4
                .byte   >_snd_data_ufo, >_snd_data_ufohit
snd_len_lo:     .byte   0, <1635, <1862, <4330, <2623, <245, <260, <263, <274, <898, <5632
snd_len_hi:     .byte   0, >1635, >1862, >4330, >2623, >245, >260, >263, >274, >898, >5632
snd_clock:      .byte   0, 1, 1, 1, 2, 2, 2, 2, 2, 1, 2   ; timer-7 clock select
snd_loopf:      .byte   0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0   ; ufo loops
snd_adpcmf:     .byte   0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1   ; shoot/killed/explosion
snd_prio:       .byte   0, 30, 40, 60, 50, 25, 25, 25, 25, 20, 45

; IMA-ADPCM step table (89 entries) and index deltas.
snd_idx_delta:  .byte   $FF, $FF, $FF, $FF, 2, 4, 6, 8   ; -1 x4, +2, +4, +6, +8

        .code

; --- void snd_init (void) ------------------------------------------------------
_snd_init:
        lda     #0
        sta     MSTEREO         ; connect all channels
        sta     $FD44           ; no attenuation / panning
        sta     AUD0VOL
        sta     AUD0FEED        ; no feedback taps: LFSR input stays 1
        sta     AUD0CTLA        ; channel timer stopped while setting up
        sta     TIM7CTLA        ; sample timer stopped
        sta     _snd_active
        sta     _snd_cur
        sta     snd_cur_prio
        lda     #$FF
        sta     AUD0SHIFT       ; shifter low = all ones
        lda     #$F0
        sta     AUD0CTLB        ; shifter bits 11..8 = all ones
        lda     #22
        sta     AUD0BKUP        ; period 23 us: the user's pick of
                                ; a 181/90/44/22 sweep on an older
                                ; emulator build (2026-11-23) -- fine
                                ; enough latch rate without pinning
                                ; its scheduler like BKUP=1 did
                                ; period: that build latches the DAC
                                ; output on channel-timer borrows, so
                                ; borrow rate must match sample rate
                                ; (BKUP=0: never processed, silent;
                                ; BKUP=1: latched at scheduler mercy,
                                ; choppy).  On hardware the output
                                ; follows VOL at any period."
        lda     #$18            ; RELOAD + CNTEN + clock select 0 (1 us)
        sta     AUD0CTLA        ; run: output value follows +VOL
        rts

; --- void snd_stop (void) ------------------------------------------------------
_snd_stop:
        php
        sei
        lda     #0
        sta     _snd_active
        sta     _snd_cur
        sta     snd_cur_prio
        sta     AUD0VOL
        sta     TIM7CTLA
        plp
        rts

; --- void __fastcall__ snd_play (unsigned char id) ------------------------------
_snd_play:
        tax
        beq     @ret            ; id 0 = none
        lda     snd_prio,x
        ldy     _snd_active
        beq     @go
        cmp     snd_cur_prio
        bcc     @ret            ; lower priority than current: drop
@go:    php
        sei
        stx     _snd_cur
        sta     snd_cur_prio
        lda     snd_ptr_lo,x
        sta     snd_start
        sta     snd_ptr
        lda     snd_ptr_hi,x
        sta     snd_start+1
        sta     snd_ptr+1
        lda     snd_len_lo,x
        sta     snd_len
        sta     snd_rem
        lda     snd_len_hi,x
        sta     snd_len+1
        sta     snd_rem+1
        lda     snd_loopf,x
        sta     snd_loop
        lda     snd_adpcmf,x
        sta     snd_adpcm_cur
        lda     #0              ; ADPCM state reset
        sta     snd_pred
        sta     snd_pred+1
        sta     snd_index
        sta     snd_nibflag
        sta     AUD0VOL         ; silence until the first tick
        sta     TIM7CTLA        ; stop the sample timer
        lda     #90
        sta     TIM7BKUP
        lda     snd_clock,x
        ora     #$98            ; irq enable + reload + count enable
        sta     TIM7CTLA
        lda     #1
        sta     _snd_active
        plp
@ret:   rts

; --- the sample feeder -----------------------------------------------------------
snd_irq:
        lda     INTSET
        and     #SND_INTERRUPT  ; timer 7?
        beq     @notmine
        lda     _snd_active
        beq     @stale
        lda     snd_adpcm_cur
        bne     @adpcm
        ldy     #0              ; raw: one byte per tick
        lda     (snd_ptr),y     ; unsigned sample, 128 = center
        eor     #$80            ; -> signed for the volume register
        sta     AUD0VOL
        inc     snd_ptr
        bne     @count
        inc     snd_ptr+1
        bra     @count

@adpcm: jsr     snd_decode      ; returns the output byte in A
        sta     AUD0VOL

@count: sec                     ; one sample consumed
        lda     snd_rem
        sbc     #1
        sta     snd_rem
        lda     snd_rem+1
        sbc     #0
        sta     snd_rem+1
        ora     snd_rem
        bne     @done
        lda     snd_loop        ; sample finished
        bne     @rewind
        lda     #0              ; one-shot: silence and stop
        sta     _snd_active
        sta     _snd_cur
        sta     snd_cur_prio
        sta     AUD0VOL
        sta     TIM7CTLA
        bra     @done
@rewind:
        lda     snd_start
        sta     snd_ptr
        lda     snd_start+1
        sta     snd_ptr+1
        lda     snd_len
        sta     snd_rem
        lda     snd_len+1
        sta     snd_rem+1
        lda     #0              ; ADPCM state restarts with the loop
        sta     snd_pred
        sta     snd_pred+1
        sta     snd_index
        sta     snd_nibflag
@done:  sec                     ; handled
        rts
@stale: lda     #0              ; timer running with nothing to play
        sta     TIM7CTLA
        sec
        rts
@notmine:
        clc
        rts

; --- IMA-ADPCM decode of one sample; output byte (VOL-ready) in A -------------
snd_decode:
        lda     snd_nibflag     ; fetch the next nibble
        bne     @hi
        ldy     #0
        lda     (snd_ptr),y
        sta     snd_nibbyte
        inc     snd_ptr
        bne     @lo
        inc     snd_ptr+1
@lo:    lda     #1
        sta     snd_nibflag
        lda     snd_nibbyte
        and     #$0F
        bra     @have
@hi:    lda     #0
        sta     snd_nibflag
        lda     snd_nibbyte
        lsr     a
        lsr     a
        lsr     a
        lsr     a
@have:  sta     snd_nibsave     ; keep the nibble
        lda     snd_index
        sta     snd_old_index   ; step for THIS sample uses the old index
        lda     snd_nibsave
        and     #$07
        tay
        lda     snd_idx_delta,y ; step-index update (clamped below)
        clc
        adc     snd_index
        bpl     @iok1
        lda     #0
@iok1:  cmp     #89
        bcc     @iok2
        lda     #88
@iok2:  sta     snd_index_new
        lda     snd_index_new
        sta     snd_index
        ; vpdiff = vpdiff_tab[old_index*8 + (nibble & 7)] (word table)
        lda     #0
        sta     snd_vp+1
        lda     snd_old_index
        asl     a
        rol     snd_vp+1
        asl     a
        rol     snd_vp+1
        asl     a
        rol     snd_vp+1
        asl     a
        rol     snd_vp+1
        sta     snd_vp          ; snd_vp = old_index * 16
        lda     snd_nibsave
        and     #$07
        asl     a               ; (nibble & 7) * 2
        clc
        adc     snd_vp
        sta     snd_vp
        bcc     @vpt1
        inc     snd_vp+1
@vpt1:  lda     snd_vp
        clc
        adc     #<_snd_vpdiff_tab
        sta     vp_ptr
        lda     snd_vp+1
        adc     #>_snd_vpdiff_tab
        sta     vp_ptr+1
        ldy     #0
        lda     (vp_ptr),y
        sta     snd_vp
        iny
        lda     (vp_ptr),y
        sta     snd_vp+1        ; snd_vp = vpdiff
        lda     snd_nibsave
        and     #$08
        bne     @sub
        ; pred += vpdiff, saturate at +32767.  NOTE: vpdiff can exceed
        ; 32767 (max step 32767 * 1.875), so the ADC V flag is useless
        ; here (it treats vpdiff as a signed value).  Compare vpdiff
        ; against the unsigned headroom 32767 - pred instead.
        sec
        lda     #$FF
        sbc     snd_pred
        sta     snd_tmp         ; headroom lo
        lda     #$7F
        sbc     snd_pred+1
        sta     snd_tmp+1       ; headroom hi
        lda     snd_vp+1
        cmp     snd_tmp+1
        bcc     @addok
        bne     @addmax
        lda     snd_vp
        cmp     snd_tmp
        bcc     @addok
        beq     @addok
@addmax:
        lda     #$FF
        sta     snd_pred
        lda     #$7F
        sta     snd_pred+1
        bra     @out
@addok: clc
        lda     snd_pred
        adc     snd_vp
        sta     snd_pred
        lda     snd_pred+1
        adc     snd_vp+1
        sta     snd_pred+1
        bra     @out
@sub:   ; pred -= vpdiff, saturate at -32768 (same headroom method:
        ; headroom = pred + 32768, i.e. pred with its sign bit flipped)
        lda     snd_pred+1
        eor     #$80
        sta     snd_tmp+1       ; headroom hi (lo = snd_pred itself)
        lda     snd_vp+1
        cmp     snd_tmp+1
        bcc     @subok
        bne     @submax
        lda     snd_vp
        cmp     snd_pred
        bcc     @subok
        beq     @subok
@submax:
        lda     #$00
        sta     snd_pred
        lda     #$80
        sta     snd_pred+1
        bra     @out
@subok: sec
        lda     snd_pred
        sbc     snd_vp
        sta     snd_pred
        lda     snd_pred+1
        sbc     snd_vp+1
        sta     snd_pred+1
@out:   lda     snd_pred+1      ; output = pred >> 8 (arithmetic); the
                                ; predictor is already signed, exactly the
                                ; form the volume register wants (no EOR)
        rts

        .bss
snd_index_new:  .res    1
