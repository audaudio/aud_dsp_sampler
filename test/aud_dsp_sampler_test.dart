import 'package:test/test.dart';

import 'package:aud_dsp_sampler/aud_dsp_sampler.dart';

void main() {
  test('invoke native function', () {
    expect(sum(24, 18), 42);
  });

  test('invoke async native callback', () async {
    expect(await sumAsync(24, 18), 42);
  });
}
