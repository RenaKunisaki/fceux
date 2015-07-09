print("frameskip=", config['SDL.Frameskip'])
while true do
	gui.text(10, 10, "butt")
	gui.drawline(-100, -100, 1000, 500, '#FF0000')
	gui.drawbox(23, 16, 23+(6*8), 16+(2*8), '#007F0080', '#00000080')
	gui.drawline(-5, 20, 260, 200, '#00FFFF')
	--gui.drawbox(0, 0, 20, 20, '#FF0000', false)
	gui.drawbox(0, 0, 20, 20, false, '#FFFFFF')
	emu.frameadvance()
end
