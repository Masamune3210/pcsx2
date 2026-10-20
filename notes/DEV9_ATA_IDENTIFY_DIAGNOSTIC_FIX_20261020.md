# IDENTIFY strings and diagnostic signature

ATA/ATAPI-4 T13/1153D rev18 §8.12.8 specifies the first ASCII character on
DD15:8 and the second on DD7:0. The previous IDENTIFY helper copied ASCII
directly into the little-endian PIO buffer. A guest decoding the words by the
standard consequently saw each serial, firmware and model character pair reversed.
The helper now swaps each padded byte pair before transfer; numeric fields retain
their existing encoding. No identity text or disk contents change.

Section 9.1 requires Device/Head=00h in the ATA signature after EXECUTE DEVICE
DIAGNOSTIC as well as resets. The standalone 90h path previously cleared DEV
only, retaining the old LBA and head bits. The common diagnostic routine now
writes the entire Device/Head signature alongside Count=1, Sector=1 and both
Cylinder bytes=0. This also works when the absent device1 was selected, since
90h addresses both devices. Reset behavior uses the same routine.

Two prospective public-register regression cases were written before production
changes: decode all three padded strings from their DATA16 words, and issue 90h
from both device selections with nonzero LBA/head bits, checking the complete
signature, success/error-code1 and IRQ. Source inspection and whitespace checks
were performed. No compilation, CMake configuration, C++ test execution, Windows
build or guest boot was run, as requested. These fixes still need runtime validation.
