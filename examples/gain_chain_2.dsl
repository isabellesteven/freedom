graph "gain_chain_2"
  io input  mic : f32@32k block=20 ch=1
  io output spk : f32@32k block=20 ch=1

  node g1 : Gain(gain_db=-6.0)
  node g2 : Gain(gain_db=3.0)

  connect mic -> g1.in
  connect g1.out -> g2.in
  connect g2.out -> spk
end