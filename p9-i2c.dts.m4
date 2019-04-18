define(`I2CBUS', `i2c_bus@$1 {
bus-frequency = <0x61a80>;
compatible = "ibm,opal-i2c", "ibm,power8-i2c-port", "ibm,power9-i2c-port";
index = <$1>;
reg = <$1>;
}')dnl

I2CBUS(0);
I2CBUS(1);
I2CBUS(2);
I2CBUS(3);
I2CBUS(4);
I2CBUS(5);
I2CBUS(6);
I2CBUS(7);
I2CBUS(8);
I2CBUS(9);
I2CBUS(10);
I2CBUS(11);
I2CBUS(12);
I2CBUS(13);
