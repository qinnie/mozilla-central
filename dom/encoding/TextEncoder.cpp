/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/TextEncoder.h"
#include "mozilla/dom/EncodingUtils.h"
#include "nsContentUtils.h"
#include "nsICharsetConverterManager.h"
#include "nsServiceManagerUtils.h"

namespace mozilla {
namespace dom {

void
TextEncoder::Init(const nsAString& aEncoding,
                  ErrorResult& aRv)
{
  nsAutoString label(aEncoding);
  EncodingUtils::TrimSpaceCharacters(label);

  // Let encoding be the result of getting an encoding from label.
  // If encoding is failure, or is none of utf-8, utf-16, and utf-16be,
  // throw a TypeError.
  if (!EncodingUtils::FindEncodingForLabel(label, mEncoding)) {
    aRv.ThrowTypeError(MSG_ENCODING_NOT_SUPPORTED, &label);
    return;
  }

  if (!mEncoding.EqualsLiteral("UTF-8") &&
      !mEncoding.EqualsLiteral("UTF-16LE") &&
      !mEncoding.EqualsLiteral("UTF-16BE")) {
    aRv.ThrowTypeError(MSG_DOM_ENCODING_NOT_UTF);
    return;
  }

  // Create an encoder object for mEncoding.
  nsCOMPtr<nsICharsetConverterManager> ccm =
    do_GetService(NS_CHARSETCONVERTERMANAGER_CONTRACTID);
  if (!ccm) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return;
  }

  ccm->GetUnicodeEncoderRaw(mEncoding.get(), getter_AddRefs(mEncoder));
  if (!mEncoder) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return;
  }
}

JSObject*
TextEncoder::Encode(JSContext* aCx,
                    const nsAString& aString,
                    const TextEncodeOptions& aOptions,
                    ErrorResult& aRv)
{
  // Run the steps of the encoding algorithm.
  int32_t srcLen = aString.Length();
  int32_t maxLen;
  const PRUnichar* data = PromiseFlatString(aString).get();
  nsresult rv = mEncoder->GetMaxLength(data, srcLen, &maxLen);
  if (NS_FAILED(rv)) {
    aRv.Throw(rv);
    return nullptr;
  }
  // Need a fallible allocator because the caller may be a content
  // and the content can specify the length of the string.
  static const fallible_t fallible = fallible_t();
  nsAutoArrayPtr<char> buf(new (fallible) char[maxLen + 1]);
  if (!buf) {
    aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
    return nullptr;
  }

  int32_t dstLen = maxLen;
  rv = mEncoder->Convert(data, &srcLen, buf, &dstLen);

  // If the internal streaming flag is not set, then reset
  // the encoding algorithm state to the default values for encoding.
  if (!aOptions.stream) {
    int32_t finishLen = maxLen - dstLen;
    rv = mEncoder->Finish(buf + dstLen, &finishLen);
    if (NS_SUCCEEDED(rv)) {
      dstLen += finishLen;
    }
  }

  JSObject* outView = nullptr;
  if (NS_SUCCEEDED(rv)) {
    buf[dstLen] = '\0';
    outView = Uint8Array::Create(aCx, this, dstLen,
                                 reinterpret_cast<uint8_t*>(buf.get()));
  }

  if (NS_FAILED(rv)) {
    aRv.Throw(rv);
  }
  return outView;
}

void
TextEncoder::GetEncoding(nsAString& aEncoding)
{
  // Our utf-16 converter does not comply with the Encoding Standard.
  // As a result the utf-16le converter is used for the encoding label
  // "utf-16".
  // This workaround should not be exposed to the public API and so "utf-16"
  // is returned by GetEncoding() if the internal encoding name is "utf-16le".
  if (mEncoding.EqualsLiteral("UTF-16LE")) {
    aEncoding.AssignLiteral("utf-16");
    return;
  }

  CopyASCIItoUTF16(mEncoding, aEncoding);
  nsContentUtils::ASCIIToLower(aEncoding);
}

NS_IMPL_CYCLE_COLLECTING_ADDREF(TextEncoder)
NS_IMPL_CYCLE_COLLECTING_RELEASE(TextEncoder)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(TextEncoder)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_1(TextEncoder, mGlobal)

} // dom
} // mozilla
