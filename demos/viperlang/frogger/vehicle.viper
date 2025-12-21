module Vehicle;

import "./config";
import "./position";

// Vehicle types
Integer TYPE_CAR = 1;
Integer TYPE_TRUCK = 2;
Integer TYPE_SPORTS_CAR = 3;

// Vehicle entity - represents cars and trucks on the road
entity Vehicle {
    expose Position pos;
    expose Integer speed;
    expose Integer direction;  // 1 = right, -1 = left
    expose Integer width;
    expose Integer vehicleType;

    expose func init(Integer row, Integer col, Integer spd, Integer dir, Integer w, Integer vType) {
        pos = createPosition(row, col);
        speed = spd;
        direction = dir;
        width = w;
        vehicleType = vType;
    }

    expose func move() {
        Integer delta = speed * direction;
        Integer newCol = pos.col + delta;

        // Wrap around screen edges
        if (direction > 0) {
            // Moving right
            if (newCol > GAME_WIDTH + width) {
                newCol = 0 - width;
            }
        } else {
            // Moving left
            if (newCol < (0 - width)) {
                newCol = GAME_WIDTH + width;
            }
        }

        pos.col = newCol;
    }

    expose func checkCollision(Integer frogRow, Integer frogCol) -> Boolean {
        if (frogRow != pos.row) {
            return false;
        }

        // Check if frog is within vehicle width
        var i = 0;
        while (i < width) {
            Integer checkCol = pos.col + i;
            if (frogCol == checkCol) {
                return true;
            }
            i = i + 1;
        }
        return false;
    }

    expose func getRow() -> Integer {
        return pos.row;
    }

    expose func getCol() -> Integer {
        return pos.col;
    }

    expose func getWidth() -> Integer {
        return width;
    }

    expose func getType() -> Integer {
        return vehicleType;
    }

    expose func isCar() -> Boolean {
        return vehicleType == TYPE_CAR || vehicleType == TYPE_SPORTS_CAR;
    }

    expose func isTruck() -> Boolean {
        return vehicleType == TYPE_TRUCK;
    }

    expose func isSportsCar() -> Boolean {
        return vehicleType == TYPE_SPORTS_CAR;
    }
}

// Factory functions for different vehicle types
func createCar(Integer row, Integer col, Integer speed, Integer direction) -> Vehicle {
    Vehicle v = new Vehicle();
    v.init(row, col, speed, direction, 4, TYPE_CAR);
    return v;
}

func createTruck(Integer row, Integer col, Integer speed, Integer direction) -> Vehicle {
    Vehicle v = new Vehicle();
    v.init(row, col, speed, direction, 6, TYPE_TRUCK);
    return v;
}

func createSportsCar(Integer row, Integer col, Integer speed, Integer direction) -> Vehicle {
    Vehicle v = new Vehicle();
    v.init(row, col, speed, direction, 3, TYPE_SPORTS_CAR);
    return v;
}
