## Installation

```sh
docker pull indreshp135/networkslab:latest

docker run -ti  --net=host --env="DISPLAY" --volume="$HOME/.Xauthority:/root/.Xauthority:rw" --volume="$HOME/Desktop/nLab/NS2/scripts:/labs" indreshp135/networkslab

```
