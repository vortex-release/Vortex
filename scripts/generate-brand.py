# Export the code-native Vortex mark as a Windows multi-resolution icon.
from pathlib import Path
from PIL import Image,ImageDraw
root=Path(__file__).resolve().parents[1]
size=1024
im=Image.new("RGBA",(size,size),(0,0,0,0));draw=ImageDraw.Draw(im)
left=[(3,5),(10,5),(16,19),(12.5,27)];right=[(22,5),(29,5),(18,27),(12.5,27)]
for points,color in [(left,"#f0eff8"),(right,"#b4a0f2")]:
    draw.polygon([(x*size/32,y*size/32) for x,y in points],fill=color)
im.save(root/"app/vortex.ico",sizes=[(16,16),(24,24),(32,32),(48,48),(64,64),(128,128),(256,256)])
