/dts-v1/;

/ {
	#address-cells = <0x1>;
	#size-cells = <0x0>;

	/* Host based debugfs access */
	pib@0 {
	      #address-cells = <0x2>;
	      #size-cells = <0x1>;
	      compatible = "ibm,host-pib";
	      reg = <0x0>;
	      index = <0x0>;
	      include(p9-pib.dts.m4)dnl

		  i2cm@a1000 {
			#address-cells = <0x1>;
			#size-cells = <0x0>;
			reg = <0x0 0xa1000 0x400>;
			compatible = "ibm,power9-i2cm";
			include(p9-i2c.dts.m4)dnl
		  };
		  i2cm@a2000 {
			#address-cells = <0x1>;
			#size-cells = <0x0>;
			reg = <0x0 0xa2000 0x400>;
			compatible = "ibm,power9-i2cm";
			include(p9-i2c.dts.m4)dnl
		  };
		  i2cm@a3000 {
			#address-cells = <0x1>;
			#size-cells = <0x0>;
			reg = <0x0 0xa3000 0x400>;
			compatible = "ibm,power9-i2cm";
			include(p9-i2c.dts.m4)dnl
		  };
	};

	pib@8 {
	      #address-cells = <0x2>;
	      #size-cells = <0x1>;
	      compatible = "ibm,host-pib";
	      reg = <0x8>;
	      index = <0x8>;
	      include(p9-pib.dts.m4)dnl

		  i2cm@a1000 {
			#address-cells = <0x1>;
			#size-cells = <0x0>;
			reg = <0x0 0xa1000 0x400>;
			compatible = "ibm,power9-i2cm";
			include(p9-i2c.dts.m4)dnl
		  };
		  i2cm@a2000 {
			#address-cells = <0x1>;
			#size-cells = <0x0>;
			reg = <0x0 0xa2000 0x400>;
			compatible = "ibm,power9-i2cm";
			include(p9-i2c.dts.m4)dnl
		  };
		  i2cm@a3000 {
			#address-cells = <0x1>;
			#size-cells = <0x0>;
			reg = <0x0 0xa3000 0x400>;
			compatible = "ibm,power9-i2cm";
			include(p9-i2c.dts.m4)dnl
		  };


	};
};
