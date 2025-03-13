/*
  DON, a UCI chess playing engine derived from Stockfish

  DON is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  DON is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "polybook.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <iostream>

#include "bitboard.h"
#include "misc.h"
#include "movegen.h"
#include "position.h"
#include "uci.h"

namespace DON {

PolyBook Book;

namespace {

// Random numbers from PolyGlot, used to compute book hash keys
const union PolyGlot {
    Key Randoms[781];

    struct {
        Key psq[COLOR_NB * 6][SQUARE_NB];  // [piece][square]
        Key castling[COLOR_NB * 2];        // [castle-right]
        Key enpassant[FILE_NB];            // [file]
        Key side;
    } Zobrist;
} PG = {{0x9D39247E33776D41ull, 0x2AF7398005AAA5C7ull, 0x44DB015024623547ull, 0x9C15F73E62A76AE2ull,
         0x75834465489C0C89ull, 0x3290AC3A203001BFull, 0x0FBBAD1F61042279ull, 0xE83A908FF2FB60CAull,
         0x0D7E765D58755C10ull, 0x1A083822CEAFE02Dull, 0x9605D5F0E25EC3B0ull, 0xD021FF5CD13A2ED5ull,
         0x40BDF15D4A672E32ull, 0x011355146FD56395ull, 0x5DB4832046F3D9E5ull, 0x239F8B2D7FF719CCull,
         0x05D1A1AE85B49AA1ull, 0x679F848F6E8FC971ull, 0x7449BBFF801FED0Bull, 0x7D11CDB1C3B7ADF0ull,
         0x82C7709E781EB7CCull, 0xF3218F1C9510786Cull, 0x331478F3AF51BBE6ull, 0x4BB38DE5E7219443ull,
         0xAA649C6EBCFD50FCull, 0x8DBD98A352AFD40Bull, 0x87D2074B81D79217ull, 0x19F3C751D3E92AE1ull,
         0xB4AB30F062B19ABFull, 0x7B0500AC42047AC4ull, 0xC9452CA81A09D85Dull, 0x24AA6C514DA27500ull,
         0x4C9F34427501B447ull, 0x14A68FD73C910841ull, 0xA71B9B83461CBD93ull, 0x03488B95B0F1850Full,
         0x637B2B34FF93C040ull, 0x09D1BC9A3DD90A94ull, 0x3575668334A1DD3Bull, 0x735E2B97A4C45A23ull,
         0x18727070F1BD400Bull, 0x1FCBACD259BF02E7ull, 0xD310A7C2CE9B6555ull, 0xBF983FE0FE5D8244ull,
         0x9F74D14F7454A824ull, 0x51EBDC4AB9BA3035ull, 0x5C82C505DB9AB0FAull, 0xFCF7FE8A3430B241ull,
         0x3253A729B9BA3DDEull, 0x8C74C368081B3075ull, 0xB9BC6C87167C33E7ull, 0x7EF48F2B83024E20ull,
         0x11D505D4C351BD7Full, 0x6568FCA92C76A243ull, 0x4DE0B0F40F32A7B8ull, 0x96D693460CC37E5Dull,
         0x42E240CB63689F2Full, 0x6D2BDCDAE2919661ull, 0x42880B0236E4D951ull, 0x5F0F4A5898171BB6ull,
         0x39F890F579F92F88ull, 0x93C5B5F47356388Bull, 0x63DC359D8D231B78ull, 0xEC16CA8AEA98AD76ull,
         0x5355F900C2A82DC7ull, 0x07FB9F855A997142ull, 0x5093417AA8A7ED5Eull, 0x7BCBC38DA25A7F3Cull,
         0x19FC8A768CF4B6D4ull, 0x637A7780DECFC0D9ull, 0x8249A47AEE0E41F7ull, 0x79AD695501E7D1E8ull,
         0x14ACBAF4777D5776ull, 0xF145B6BECCDEA195ull, 0xDABF2AC8201752FCull, 0x24C3C94DF9C8D3F6ull,
         0xBB6E2924F03912EAull, 0x0CE26C0B95C980D9ull, 0xA49CD132BFBF7CC4ull, 0xE99D662AF4243939ull,
         0x27E6AD7891165C3Full, 0x8535F040B9744FF1ull, 0x54B3F4FA5F40D873ull, 0x72B12C32127FED2Bull,
         0xEE954D3C7B411F47ull, 0x9A85AC909A24EAA1ull, 0x70AC4CD9F04F21F5ull, 0xF9B89D3E99A075C2ull,
         0x87B3E2B2B5C907B1ull, 0xA366E5B8C54F48B8ull, 0xAE4A9346CC3F7CF2ull, 0x1920C04D47267BBDull,
         0x87BF02C6B49E2AE9ull, 0x092237AC237F3859ull, 0xFF07F64EF8ED14D0ull, 0x8DE8DCA9F03CC54Eull,
         0x9C1633264DB49C89ull, 0xB3F22C3D0B0B38EDull, 0x390E5FB44D01144Bull, 0x5BFEA5B4712768E9ull,
         0x1E1032911FA78984ull, 0x9A74ACB964E78CB3ull, 0x4F80F7A035DAFB04ull, 0x6304D09A0B3738C4ull,
         0x2171E64683023A08ull, 0x5B9B63EB9CEFF80Cull, 0x506AACF489889342ull, 0x1881AFC9A3A701D6ull,
         0x6503080440750644ull, 0xDFD395339CDBF4A7ull, 0xEF927DBCF00C20F2ull, 0x7B32F7D1E03680ECull,
         0xB9FD7620E7316243ull, 0x05A7E8A57DB91B77ull, 0xB5889C6E15630A75ull, 0x4A750A09CE9573F7ull,
         0xCF464CEC899A2F8Aull, 0xF538639CE705B824ull, 0x3C79A0FF5580EF7Full, 0xEDE6C87F8477609Dull,
         0x799E81F05BC93F31ull, 0x86536B8CF3428A8Cull, 0x97D7374C60087B73ull, 0xA246637CFF328532ull,
         0x043FCAE60CC0EBA0ull, 0x920E449535DD359Eull, 0x70EB093B15B290CCull, 0x73A1921916591CBDull,
         0x56436C9FE1A1AA8Dull, 0xEFAC4B70633B8F81ull, 0xBB215798D45DF7AFull, 0x45F20042F24F1768ull,
         0x930F80F4E8EB7462ull, 0xFF6712FFCFD75EA1ull, 0xAE623FD67468AA70ull, 0xDD2C5BC84BC8D8FCull,
         0x7EED120D54CF2DD9ull, 0x22FE545401165F1Cull, 0xC91800E98FB99929ull, 0x808BD68E6AC10365ull,
         0xDEC468145B7605F6ull, 0x1BEDE3A3AEF53302ull, 0x43539603D6C55602ull, 0xAA969B5C691CCB7Aull,
         0xA87832D392EFEE56ull, 0x65942C7B3C7E11AEull, 0xDED2D633CAD004F6ull, 0x21F08570F420E565ull,
         0xB415938D7DA94E3Cull, 0x91B859E59ECB6350ull, 0x10CFF333E0ED804Aull, 0x28AED140BE0BB7DDull,
         0xC5CC1D89724FA456ull, 0x5648F680F11A2741ull, 0x2D255069F0B7DAB3ull, 0x9BC5A38EF729ABD4ull,
         0xEF2F054308F6A2BCull, 0xAF2042F5CC5C2858ull, 0x480412BAB7F5BE2Aull, 0xAEF3AF4A563DFE43ull,
         0x19AFE59AE451497Full, 0x52593803DFF1E840ull, 0xF4F076E65F2CE6F0ull, 0x11379625747D5AF3ull,
         0xBCE5D2248682C115ull, 0x9DA4243DE836994Full, 0x066F70B33FE09017ull, 0x4DC4DE189B671A1Cull,
         0x51039AB7712457C3ull, 0xC07A3F80C31FB4B4ull, 0xB46EE9C5E64A6E7Cull, 0xB3819A42ABE61C87ull,
         0x21A007933A522A20ull, 0x2DF16F761598AA4Full, 0x763C4A1371B368FDull, 0xF793C46702E086A0ull,
         0xD7288E012AEB8D31ull, 0xDE336A2A4BC1C44Bull, 0x0BF692B38D079F23ull, 0x2C604A7A177326B3ull,
         0x4850E73E03EB6064ull, 0xCFC447F1E53C8E1Bull, 0xB05CA3F564268D99ull, 0x9AE182C8BC9474E8ull,
         0xA4FC4BD4FC5558CAull, 0xE755178D58FC4E76ull, 0x69B97DB1A4C03DFEull, 0xF9B5B7C4ACC67C96ull,
         0xFC6A82D64B8655FBull, 0x9C684CB6C4D24417ull, 0x8EC97D2917456ED0ull, 0x6703DF9D2924E97Eull,
         0xC547F57E42A7444Eull, 0x78E37644E7CAD29Eull, 0xFE9A44E9362F05FAull, 0x08BD35CC38336615ull,
         0x9315E5EB3A129ACEull, 0x94061B871E04DF75ull, 0xDF1D9F9D784BA010ull, 0x3BBA57B68871B59Dull,
         0xD2B7ADEEDED1F73Full, 0xF7A255D83BC373F8ull, 0xD7F4F2448C0CEB81ull, 0xD95BE88CD210FFA7ull,
         0x336F52F8FF4728E7ull, 0xA74049DAC312AC71ull, 0xA2F61BB6E437FDB5ull, 0x4F2A5CB07F6A35B3ull,
         0x87D380BDA5BF7859ull, 0x16B9F7E06C453A21ull, 0x7BA2484C8A0FD54Eull, 0xF3A678CAD9A2E38Cull,
         0x39B0BF7DDE437BA2ull, 0xFCAF55C1BF8A4424ull, 0x18FCF680573FA594ull, 0x4C0563B89F495AC3ull,
         0x40E087931A00930Dull, 0x8CFFA9412EB642C1ull, 0x68CA39053261169Full, 0x7A1EE967D27579E2ull,
         0x9D1D60E5076F5B6Full, 0x3810E399B6F65BA2ull, 0x32095B6D4AB5F9B1ull, 0x35CAB62109DD038Aull,
         0xA90B24499FCFAFB1ull, 0x77A225A07CC2C6BDull, 0x513E5E634C70E331ull, 0x4361C0CA3F692F12ull,
         0xD941ACA44B20A45Bull, 0x528F7C8602C5807Bull, 0x52AB92BEB9613989ull, 0x9D1DFA2EFC557F73ull,
         0x722FF175F572C348ull, 0x1D1260A51107FE97ull, 0x7A249A57EC0C9BA2ull, 0x04208FE9E8F7F2D6ull,
         0x5A110C6058B920A0ull, 0x0CD9A497658A5698ull, 0x56FD23C8F9715A4Cull, 0x284C847B9D887AAEull,
         0x04FEABFBBDB619CBull, 0x742E1E651C60BA83ull, 0x9A9632E65904AD3Cull, 0x881B82A13B51B9E2ull,
         0x506E6744CD974924ull, 0xB0183DB56FFC6A79ull, 0x0ED9B915C66ED37Eull, 0x5E11E86D5873D484ull,
         0xF678647E3519AC6Eull, 0x1B85D488D0F20CC5ull, 0xDAB9FE6525D89021ull, 0x0D151D86ADB73615ull,
         0xA865A54EDCC0F019ull, 0x93C42566AEF98FFBull, 0x99E7AFEABE000731ull, 0x48CBFF086DDF285Aull,
         0x7F9B6AF1EBF78BAFull, 0x58627E1A149BBA21ull, 0x2CD16E2ABD791E33ull, 0xD363EFF5F0977996ull,
         0x0CE2A38C344A6EEDull, 0x1A804AADB9CFA741ull, 0x907F30421D78C5DEull, 0x501F65EDB3034D07ull,
         0x37624AE5A48FA6E9ull, 0x957BAF61700CFF4Eull, 0x3A6C27934E31188Aull, 0xD49503536ABCA345ull,
         0x088E049589C432E0ull, 0xF943AEE7FEBF21B8ull, 0x6C3B8E3E336139D3ull, 0x364F6FFA464EE52Eull,
         0xD60F6DCEDC314222ull, 0x56963B0DCA418FC0ull, 0x16F50EDF91E513AFull, 0xEF1955914B609F93ull,
         0x565601C0364E3228ull, 0xECB53939887E8175ull, 0xBAC7A9A18531294Bull, 0xB344C470397BBA52ull,
         0x65D34954DAF3CEBDull, 0xB4B81B3FA97511E2ull, 0xB422061193D6F6A7ull, 0x071582401C38434Dull,
         0x7A13F18BBEDC4FF5ull, 0xBC4097B116C524D2ull, 0x59B97885E2F2EA28ull, 0x99170A5DC3115544ull,
         0x6F423357E7C6A9F9ull, 0x325928EE6E6F8794ull, 0xD0E4366228B03343ull, 0x565C31F7DE89EA27ull,
         0x30F5611484119414ull, 0xD873DB391292ED4Full, 0x7BD94E1D8E17DEBCull, 0xC7D9F16864A76E94ull,
         0x947AE053EE56E63Cull, 0xC8C93882F9475F5Full, 0x3A9BF55BA91F81CAull, 0xD9A11FBB3D9808E4ull,
         0x0FD22063EDC29FCAull, 0xB3F256D8ACA0B0B9ull, 0xB03031A8B4516E84ull, 0x35DD37D5871448AFull,
         0xE9F6082B05542E4Eull, 0xEBFAFA33D7254B59ull, 0x9255ABB50D532280ull, 0xB9AB4CE57F2D34F3ull,
         0x693501D628297551ull, 0xC62C58F97DD949BFull, 0xCD454F8F19C5126Aull, 0xBBE83F4ECC2BDECBull,
         0xDC842B7E2819E230ull, 0xBA89142E007503B8ull, 0xA3BC941D0A5061CBull, 0xE9F6760E32CD8021ull,
         0x09C7E552BC76492Full, 0x852F54934DA55CC9ull, 0x8107FCCF064FCF56ull, 0x098954D51FFF6580ull,
         0x23B70EDB1955C4BFull, 0xC330DE426430F69Dull, 0x4715ED43E8A45C0Aull, 0xA8D7E4DAB780A08Dull,
         0x0572B974F03CE0BBull, 0xB57D2E985E1419C7ull, 0xE8D9ECBE2CF3D73Full, 0x2FE4B17170E59750ull,
         0x11317BA87905E790ull, 0x7FBF21EC8A1F45ECull, 0x1725CABFCB045B00ull, 0x964E915CD5E2B207ull,
         0x3E2B8BCBF016D66Dull, 0xBE7444E39328A0ACull, 0xF85B2B4FBCDE44B7ull, 0x49353FEA39BA63B1ull,
         0x1DD01AAFCD53486Aull, 0x1FCA8A92FD719F85ull, 0xFC7C95D827357AFAull, 0x18A6A990C8B35EBDull,
         0xCCCB7005C6B9C28Dull, 0x3BDBB92C43B17F26ull, 0xAA70B5B4F89695A2ull, 0xE94C39A54A98307Full,
         0xB7A0B174CFF6F36Eull, 0xD4DBA84729AF48ADull, 0x2E18BC1AD9704A68ull, 0x2DE0966DAF2F8B1Cull,
         0xB9C11D5B1E43A07Eull, 0x64972D68DEE33360ull, 0x94628D38D0C20584ull, 0xDBC0D2B6AB90A559ull,
         0xD2733C4335C6A72Full, 0x7E75D99D94A70F4Dull, 0x6CED1983376FA72Bull, 0x97FCAACBF030BC24ull,
         0x7B77497B32503B12ull, 0x8547EDDFB81CCB94ull, 0x79999CDFF70902CBull, 0xCFFE1939438E9B24ull,
         0x829626E3892D95D7ull, 0x92FAE24291F2B3F1ull, 0x63E22C147B9C3403ull, 0xC678B6D860284A1Cull,
         0x5873888850659AE7ull, 0x0981DCD296A8736Dull, 0x9F65789A6509A440ull, 0x9FF38FED72E9052Full,
         0xE479EE5B9930578Cull, 0xE7F28ECD2D49EECDull, 0x56C074A581EA17FEull, 0x5544F7D774B14AEFull,
         0x7B3F0195FC6F290Full, 0x12153635B2C0CF57ull, 0x7F5126DBBA5E0CA7ull, 0x7A76956C3EAFB413ull,
         0x3D5774A11D31AB39ull, 0x8A1B083821F40CB4ull, 0x7B4A38E32537DF62ull, 0x950113646D1D6E03ull,
         0x4DA8979A0041E8A9ull, 0x3BC36E078F7515D7ull, 0x5D0A12F27AD310D1ull, 0x7F9D1A2E1EBE1327ull,
         0xDA3A361B1C5157B1ull, 0xDCDD7D20903D0C25ull, 0x36833336D068F707ull, 0xCE68341F79893389ull,
         0xAB9090168DD05F34ull, 0x43954B3252DC25E5ull, 0xB438C2B67F98E5E9ull, 0x10DCD78E3851A492ull,
         0xDBC27AB5447822BFull, 0x9B3CDB65F82CA382ull, 0xB67B7896167B4C84ull, 0xBFCED1B0048EAC50ull,
         0xA9119B60369FFEBDull, 0x1FFF7AC80904BF45ull, 0xAC12FB171817EEE7ull, 0xAF08DA9177DDA93Dull,
         0x1B0CAB936E65C744ull, 0xB559EB1D04E5E932ull, 0xC37B45B3F8D6F2BAull, 0xC3A9DC228CAAC9E9ull,
         0xF3B8B6675A6507FFull, 0x9FC477DE4ED681DAull, 0x67378D8ECCEF96CBull, 0x6DD856D94D259236ull,
         0xA319CE15B0B4DB31ull, 0x073973751F12DD5Eull, 0x8A8E849EB32781A5ull, 0xE1925C71285279F5ull,
         0x74C04BF1790C0EFEull, 0x4DDA48153C94938Aull, 0x9D266D6A1CC0542Cull, 0x7440FB816508C4FEull,
         0x13328503DF48229Full, 0xD6BF7BAEE43CAC40ull, 0x4838D65F6EF6748Full, 0x1E152328F3318DEAull,
         0x8F8419A348F296BFull, 0x72C8834A5957B511ull, 0xD7A023A73260B45Cull, 0x94EBC8ABCFB56DAEull,
         0x9FC10D0F989993E0ull, 0xDE68A2355B93CAE6ull, 0xA44CFE79AE538BBEull, 0x9D1D84FCCE371425ull,
         0x51D2B1AB2DDFB636ull, 0x2FD7E4B9E72CD38Cull, 0x65CA5B96B7552210ull, 0xDD69A0D8AB3B546Dull,
         0x604D51B25FBF70E2ull, 0x73AA8A564FB7AC9Eull, 0x1A8C1E992B941148ull, 0xAAC40A2703D9BEA0ull,
         0x764DBEAE7FA4F3A6ull, 0x1E99B96E70A9BE8Bull, 0x2C5E9DEB57EF4743ull, 0x3A938FEE32D29981ull,
         0x26E6DB8FFDF5ADFEull, 0x469356C504EC9F9Dull, 0xC8763C5B08D1908Cull, 0x3F6C6AF859D80055ull,
         0x7F7CC39420A3A545ull, 0x9BFB227EBDF4C5CEull, 0x89039D79D6FC5C5Cull, 0x8FE88B57305E2AB6ull,
         0xA09E8C8C35AB96DEull, 0xFA7E393983325753ull, 0xD6B6D0ECC617C699ull, 0xDFEA21EA9E7557E3ull,
         0xB67C1FA481680AF8ull, 0xCA1E3785A9E724E5ull, 0x1CFC8BED0D681639ull, 0xD18D8549D140CAEAull,
         0x4ED0FE7E9DC91335ull, 0xE4DBF0634473F5D2ull, 0x1761F93A44D5AEFEull, 0x53898E4C3910DA55ull,
         0x734DE8181F6EC39Aull, 0x2680B122BAA28D97ull, 0x298AF231C85BAFABull, 0x7983EED3740847D5ull,
         0x66C1A2A1A60CD889ull, 0x9E17E49642A3E4C1ull, 0xEDB454E7BADC0805ull, 0x50B704CAB602C329ull,
         0x4CC317FB9CDDD023ull, 0x66B4835D9EAFEA22ull, 0x219B97E26FFC81BDull, 0x261E4E4C0A333A9Dull,
         0x1FE2CCA76517DB90ull, 0xD7504DFA8816EDBBull, 0xB9571FA04DC089C8ull, 0x1DDC0325259B27DEull,
         0xCF3F4688801EB9AAull, 0xF4F5D05C10CAB243ull, 0x38B6525C21A42B0Eull, 0x36F60E2BA4FA6800ull,
         0xEB3593803173E0CEull, 0x9C4CD6257C5A3603ull, 0xAF0C317D32ADAA8Aull, 0x258E5A80C7204C4Bull,
         0x8B889D624D44885Dull, 0xF4D14597E660F855ull, 0xD4347F66EC8941C3ull, 0xE699ED85B0DFB40Dull,
         0x2472F6207C2D0484ull, 0xC2A1E7B5B459AEB5ull, 0xAB4F6451CC1D45ECull, 0x63767572AE3D6174ull,
         0xA59E0BD101731A28ull, 0x116D0016CB948F09ull, 0x2CF9C8CA052F6E9Full, 0x0B090A7560A968E3ull,
         0xABEEDDB2DDE06FF1ull, 0x58EFC10B06A2068Dull, 0xC6E57A78FBD986E0ull, 0x2EAB8CA63CE802D7ull,
         0x14A195640116F336ull, 0x7C0828DD624EC390ull, 0xD74BBE77E6116AC7ull, 0x804456AF10F5FB53ull,
         0xEBE9EA2ADF4321C7ull, 0x03219A39EE587A30ull, 0x49787FEF17AF9924ull, 0xA1E9300CD8520548ull,
         0x5B45E522E4B1B4EFull, 0xB49C3B3995091A36ull, 0xD4490AD526F14431ull, 0x12A8F216AF9418C2ull,
         0x001F837CC7350524ull, 0x1877B51E57A764D5ull, 0xA2853B80F17F58EEull, 0x993E1DE72D36D310ull,
         0xB3598080CE64A656ull, 0x252F59CF0D9F04BBull, 0xD23C8E176D113600ull, 0x1BDA0492E7E4586Eull,
         0x21E0BD5026C619BFull, 0x3B097ADAF088F94Eull, 0x8D14DEDB30BE846Eull, 0xF95CFFA23AF5F6F4ull,
         0x3871700761B3F743ull, 0xCA672B91E9E4FA16ull, 0x64C8E531BFF53B55ull, 0x241260ED4AD1E87Dull,
         0x106C09B972D2E822ull, 0x7FBA195410E5CA30ull, 0x7884D9BC6CB569D8ull, 0x0647DFEDCD894A29ull,
         0x63573FF03E224774ull, 0x4FC8E9560F91B123ull, 0x1DB956E450275779ull, 0xB8D91274B9E9D4FBull,
         0xA2EBEE47E2FBFCE1ull, 0xD9F1F30CCD97FB09ull, 0xEFED53D75FD64E6Bull, 0x2E6D02C36017F67Full,
         0xA9AA4D20DB084E9Bull, 0xB64BE8D8B25396C1ull, 0x70CB6AF7C2D5BCF0ull, 0x98F076A4F7A2322Eull,
         0xBF84470805E69B5Full, 0x94C3251F06F90CF3ull, 0x3E003E616A6591E9ull, 0xB925A6CD0421AFF3ull,
         0x61BDD1307C66E300ull, 0xBF8D5108E27E0D48ull, 0x240AB57A8B888B20ull, 0xFC87614BAF287E07ull,
         0xEF02CDD06FFDB432ull, 0xA1082C0466DF6C0Aull, 0x8215E577001332C8ull, 0xD39BB9C3A48DB6CFull,
         0x2738259634305C14ull, 0x61CF4F94C97DF93Dull, 0x1B6BACA2AE4E125Bull, 0x758F450C88572E0Bull,
         0x959F587D507A8359ull, 0xB063E962E045F54Dull, 0x60E8ED72C0DFF5D1ull, 0x7B64978555326F9Full,
         0xFD080D236DA814BAull, 0x8C90FD9B083F4558ull, 0x106F72FE81E2C590ull, 0x7976033A39F7D952ull,
         0xA4EC0132764CA04Bull, 0x733EA705FAE4FA77ull, 0xB4D8F77BC3E56167ull, 0x9E21F4F903B33FD9ull,
         0x9D765E419FB69F6Dull, 0xD30C088BA61EA5EFull, 0x5D94337FBFAF7F5Bull, 0x1A4E4822EB4D7A59ull,
         0x6FFE73E81B637FB3ull, 0xDDF957BC36D8B9CAull, 0x64D0E29EEA8838B3ull, 0x08DD9BDFD96B9F63ull,
         0x087E79E5A57D1D13ull, 0xE328E230E3E2B3FBull, 0x1C2559E30F0946BEull, 0x720BF5F26F4D2EAAull,
         0xB0774D261CC609DBull, 0x443F64EC5A371195ull, 0x4112CF68649A260Eull, 0xD813F2FAB7F5C5CAull,
         0x660D3257380841EEull, 0x59AC2C7873F910A3ull, 0xE846963877671A17ull, 0x93B633ABFA3469F8ull,
         0xC0C0F5A60EF4CDCFull, 0xCAF21ECD4377B28Cull, 0x57277707199B8175ull, 0x506C11B9D90E8B1Dull,
         0xD83CC2687A19255Full, 0x4A29C6465A314CD1ull, 0xED2DF21216235097ull, 0xB5635C95FF7296E2ull,
         0x22AF003AB672E811ull, 0x52E762596BF68235ull, 0x9AEBA33AC6ECC6B0ull, 0x944F6DE09134DFB6ull,
         0x6C47BEC883A7DE39ull, 0x6AD047C430A12104ull, 0xA5B1CFDBA0AB4067ull, 0x7C45D833AFF07862ull,
         0x5092EF950A16DA0Bull, 0x9338E69C052B8E7Bull, 0x455A4B4CFE30E3F5ull, 0x6B02E63195AD0CF8ull,
         0x6B17B224BAD6BF27ull, 0xD1E0CCD25BB9C169ull, 0xDE0C89A556B9AE70ull, 0x50065E535A213CF6ull,
         0x9C1169FA2777B874ull, 0x78EDEFD694AF1EEDull, 0x6DC93D9526A50E68ull, 0xEE97F453F06791EDull,
         0x32AB0EDB696703D3ull, 0x3A6853C7E70757A7ull, 0x31865CED6120F37Dull, 0x67FEF95D92607890ull,
         0x1F2B1D1F15F6DC9Cull, 0xB69E38A8965C6B65ull, 0xAA9119FF184CCCF4ull, 0xF43C732873F24C13ull,
         0xFB4A3D794A9A80D2ull, 0x3550C2321FD6109Cull, 0x371F77E76BB8417Eull, 0x6BFA9AAE5EC05779ull,
         0xCD04F3FF001A4778ull, 0xE3273522064480CAull, 0x9F91508BFFCFC14Aull, 0x049A7F41061A9E60ull,
         0xFCB6BE43A9F2FE9Bull, 0x08DE8A1C7797DA9Bull, 0x8F9887E6078735A1ull, 0xB5B4071DBFC73A66ull,
         0x230E343DFBA08D33ull, 0x43ED7F5A0FAE657Dull, 0x3A88A0FBBCB05C63ull, 0x21874B8B4D2DBC4Full,
         0x1BDEA12E35F6A8C9ull, 0x53C065C6C8E63528ull, 0xE34A1D250E7A8D6Bull, 0xD6B04D3B7651DD7Eull,
         0x5E90277E7CB39E2Dull, 0x2C046F22062DC67Dull, 0xB10BB459132D0A26ull, 0x3FA9DDFB67E2F199ull,
         0x0E09B88E1914F7AFull, 0x10E8B35AF3EEAB37ull, 0x9EEDECA8E272B933ull, 0xD4C718BC4AE8AE5Full,
         0x81536D601170FC20ull, 0x91B534F885818A06ull, 0xEC8177F83F900978ull, 0x190E714FADA5156Eull,
         0xB592BF39B0364963ull, 0x89C350C893AE7DC1ull, 0xAC042E70F8B383F2ull, 0xB49B52E587A1EE60ull,
         0xFB152FE3FF26DA89ull, 0x3E666E6F69AE2C15ull, 0x3B544EBE544C19F9ull, 0xE805A1E290CF2456ull,
         0x24B33C9D7ED25117ull, 0xE74733427B72F0C1ull, 0x0A804D18B7097475ull, 0x57E3306D881EDB4Full,
         0x4AE7D6A36EB5DBCBull, 0x2D8D5432157064C8ull, 0xD1E649DE1E7F268Bull, 0x8A328A1CEDFE552Cull,
         0x07A3AEC79624C7DAull, 0x84547DDC3E203C94ull, 0x990A98FD5071D263ull, 0x1A4FF12616EEFC89ull,
         0xF6F7FD1431714200ull, 0x30C05B1BA332F41Cull, 0x8D2636B81555A786ull, 0x46C9FEB55D120902ull,
         0xCCEC0A73B49C9921ull, 0x4E9D2827355FC492ull, 0x19EBB029435DCB0Full, 0x4659D2B743848A2Cull,
         0x963EF2C96B33BE31ull, 0x74F85198B05A2E7Dull, 0x5A0F544DD2B1FB18ull, 0x03727073C2E134B1ull,
         0xC7F6AA2DE59AEA61ull, 0x352787BAA0D7C22Full, 0x9853EAB63B5E0B35ull, 0xABBDCDD7ED5C0860ull,
         0xCF05DAF5AC8D77B0ull, 0x49CAD48CEBF4A71Eull, 0x7A4C10EC2158C4A6ull, 0xD9E92AA246BF719Eull,
         0x13AE978D09FE5557ull, 0x730499AF921549FFull, 0x4E4B705B92903BA4ull, 0xFF577222C14F0A3Aull,
         0x55B6344CF97AAFAEull, 0xB862225B055B6960ull, 0xCAC09AFBDDD2CDB4ull, 0xDAF8E9829FE96B5Full,
         0xB5FDFC5D3132C498ull, 0x310CB380DB6F7503ull, 0xE87FBB46217A360Eull, 0x2102AE466EBB1148ull,
         0xF8549E1A3AA5E00Dull, 0x07A69AFDCC42261Aull, 0xC4C118BFE78FEAAEull, 0xF9F4892ED96BD438ull,
         0x1AF3DBE25D8F45DAull, 0xF5B4B0B0D2DEEEB4ull, 0x962ACEEFA82E1C84ull, 0x046E3ECAAF453CE9ull,
         0xF05D129681949A4Cull, 0x964781CE734B3C84ull, 0x9C2ED44081CE5FBDull, 0x522E23F3925E319Eull,
         0x177E00F9FC32F791ull, 0x2BC60A63A6F3B3F2ull, 0x222BBFAE61725606ull, 0x486289DDCC3D6780ull,
         0x7DC7785B8EFDFC80ull, 0x8AF38731C02BA980ull, 0x1FAB64EA29A2DDF7ull, 0xE4D9429322CD065Aull,
         0x9DA058C67844F20Cull, 0x24C0E332B70019B0ull, 0x233003B5A6CFE6ADull, 0xD586BD01C5C217F6ull,
         0x5E5637885F29BC2Bull, 0x7EBA726D8C94094Bull, 0x0A56A5F0BFE39272ull, 0xD79476A84EE20D06ull,
         0x9E4C1269BAA4BF37ull, 0x17EFEE45B0DEE640ull, 0x1D95B0A5FCF90BC6ull, 0x93CBE0B699C2585Dull,
         0x65FA4F227A2B6D79ull, 0xD5F9E858292504D5ull, 0xC2B5A03F71471A6Full, 0x59300222B4561E00ull,
         0xCE2F8642CA0712DCull, 0x7CA9723FBB2E8988ull, 0x2785338347F2BA08ull, 0xC61BB3A141E50E8Cull,
         0x150F361DAB9DEC26ull, 0x9F6A419D382595F4ull, 0x64A53DC924FE7AC9ull, 0x142DE49FFF7A7C3Dull,
         0x0C335248857FA9E7ull, 0x0A9C32D5EAE45305ull, 0xE6C42178C4BBB92Eull, 0x71F1CE2490D20B07ull,
         0xF1BCC3D275AFE51Aull, 0xE728E8C83C334074ull, 0x96FBF83A12884624ull, 0x81A1549FD6573DA5ull,
         0x5FA7867CAF35E149ull, 0x56986E2EF3ED091Bull, 0x917F1DD5F8886C61ull, 0xD20D8C88C8FFE65Full,
         0x31D71DCE64B2C310ull, 0xF165B587DF898190ull, 0xA57E6339DD2CF3A0ull, 0x1EF6E6DBB1961EC9ull,
         0x70CC73D90BC26E24ull, 0xE21A6B35DF0C3AD7ull, 0x003A93D8B2806962ull, 0x1C99DED33CB890A1ull,
         0xCF3145DE0ADD4289ull, 0xD0E4427A5514FB72ull, 0x77C621CC9FB3A483ull, 0x67A34DAC4356550Bull,
         0xF8D626AAAF278509ull}};

std::uint16_t swap_uint16(std::uint16_t d) noexcept {
    std::uint16_t r;

    auto dst = (std::uint8_t*) (&r);
    auto src = (std::uint8_t*) (&d);

    dst[0] = src[1];
    dst[1] = src[0];

    return r;
}

std::uint32_t swap_uint32(std::uint32_t d) noexcept {
    std::uint32_t r;

    auto dst = (std::uint8_t*) (&r);
    auto src = (std::uint8_t*) (&d);

    dst[0] = src[3];
    dst[1] = src[2];
    dst[2] = src[1];
    dst[3] = src[0];

    return r;
}

std::uint64_t swap_uint64(std::uint64_t d) noexcept {
    std::uint64_t r;

    auto dst = (std::uint8_t*) (&r);
    auto src = (std::uint8_t*) (&d);

    dst[0] = src[7];
    dst[1] = src[6];
    dst[2] = src[5];
    dst[3] = src[4];
    dst[4] = src[3];
    dst[5] = src[2];
    dst[6] = src[1];
    dst[7] = src[0];

    return r;
}

void swap_polyEntry(PolyEntry* pe) noexcept {
    pe->key    = swap_uint64(pe->key);
    pe->move   = swap_uint16(pe->move);
    pe->weight = swap_uint16(pe->weight);
    pe->learn  = swap_uint32(pe->learn);
}

Key polyglot_key(const Position& pos) noexcept {
    Key key = 0;

    Bitboard occupied = pos.pieces();
    while (occupied)
    {
        Square s  = pop_lsb(occupied);
        Piece  pc = pos.piece_on(s);
        assert(is_ok(pc));
        // PolyGlot pieces are: BP = 0, WP = 1, BN = 2, ... BK = 10, WK = 11
        key ^= PG.Zobrist.psq[2 * (type_of(pc) - 1) + (color_of(pc) == WHITE)][s];
    }

    Bitboard b = pos.castling_rights();
    while (b)
        key ^= PG.Zobrist.castling[pop_lsb(b)];

    if (is_ok(pos.ep_square()))
        key ^= PG.Zobrist.enpassant[file_of(pos.ep_square())];

    if (pos.active_color() == WHITE)
        key ^= PG.Zobrist.side;

    return key;
}

Move fix_promotion(const Move& m) noexcept {
    if (int pt = (m.raw() >> 12) & 0x7)
        return Move(m.org_sq(), m.dst_sq(), PieceType(pt + 1));
    return m;
}

// A PolyGlot book move is encoded as follows:
//
// bit  0- 5: destination square (from 0 to 63)
// bit  6-11: origin square (from 0 to 63)
// bit 12-14: promotion piece (from KNIGHT == 1 to QUEEN == 4)
//
// Castling moves follow "king captures rook" representation. So in case book
// move is a promotion have to convert the representation, in all the
// other cases can directly compare with a Move after having masked out
// the special Move's flags (bit 14-15) that are not supported by PolyGlot.
//
// DON:
// bit  0- 5: destination square (from 0 to 63)
// bit  6-11: origin square (from 0 to 63)
// bit 12-13: promotion piece type - 2 (from KNIGHT-2 to QUEEN-2)
// bit 14-15: special move flag: promotion (1), en-passant (2), castling (3)
Move pg_to_move(std::uint16_t pg_move, const Position& pos) noexcept {

    Move move = fix_promotion(Move(pg_move));

    std::uint16_t moveRaw = move.raw() & ~MOVETYPE_MASK;
    // Add 'Special move' flags and verify it is legal
    for (const Move& m : MoveList<LEGAL>(pos))
        // Compare with MoveType (bit 14-15) Masked-out
        if ((m.raw() & ~MOVETYPE_MASK) == moveRaw)
            return m;

    return Move::None;
}

bool is_draw(Position& pos, const Move& m) noexcept {
    if (m == Move::None)
        return true;

    State st;
    pos.do_move(m, st);
    bool draw = pos.is_draw(pos.ply(), true, true);
    pos.undo_move(m);

    return draw;
}

}  // namespace

std::ostream& operator<<(std::ostream& os, const PolyEntry& pe) noexcept {
    // clang-format off
    os << std::right << "key: "     << u64_to_string(pe.key)  //
       << std::left  << " move: "   << std::setw(5) << std::setfill(' ') << UCI::move_to_can(fix_promotion(Move(pe.move)))
       << std::right << " weight: " << std::setw(5) << std::setfill('0') << pe.weight
       << std::right << " learn: "  << std::setw(2) << std::setfill('0') << pe.learn;
    // clang-format on
    return os;
}


PolyBook::~PolyBook() noexcept { clear(); }

void PolyBook::clear() noexcept {
    if (entries != nullptr)
    {
        free(entries);
        entries = nullptr;
    }
    enable     = false;
    entryCount = 0;
    pieces     = 0;
    failCount  = 0;
}

void PolyBook::init(const std::string& bookFile) noexcept {
    clear();

    if (bookFile.empty())
        return;

    FILE* fptr = std::fopen(bookFile.c_str(), "rb");
    if (fptr == nullptr)
    {
        UCI::print_info_string("Could not open " + bookFile);
        return;
    }

    std::fseek(fptr, 0L, SEEK_END);
    std::size_t fileSize = std::ftell(fptr);
    std::rewind(fptr);

    entries = static_cast<PolyEntry*>(malloc(fileSize));
    if (entries == nullptr)
    {
        UCI::print_info_string("Memory allocation failed: " + bookFile);
        std::fclose(fptr);
        return;
    }

    entryCount = fileSize / sizeof(PolyEntry);

    std::size_t readSize = std::fread(entries, 1, fileSize, fptr);
    std::fclose(fptr);

    if (readSize != fileSize)
    {
        UCI::print_info_string("Could not read " + bookFile);
        clear();
        return;
    }

    if (IsLittleEndian)
        for (std::size_t i = 0; i < entryCount; ++i)
            swap_polyEntry(&entries[i]);

    enable = true;

    std::string msg = "Book: " + bookFile + " with " + std::to_string(entryCount) + " entries";
    UCI::print_info_string(msg);
}

Move PolyBook::probe(Position& pos, bool bestPick) noexcept {
    assert(enabled());

    Key key = polyglot_key(pos);

    if (!can_probe(pos, key))
        return Move::None;

    find_key(key);

    if (keyData.entryCount <= 0)
    {
        ++failCount;
        return Move::None;
    }
#if !defined(NDEBUG)
    show_key_data();
#endif

    std::size_t idx;
    idx = bestPick || keyData.entryCount == 1 ? keyData.bestIndex : keyData.randIndex;

    Move m;
    m = pg_to_move(entries[idx].move, pos);
    if (keyData.entryCount == 1 || !is_draw(pos, m))
        return m;

    idx = idx == keyData.begIndex ? keyData.begIndex + 1 : keyData.begIndex;

    m = pg_to_move(entries[idx].move, pos);
    if (keyData.entryCount == 2 || !is_draw(pos, m))
        return m;

    idx = idx != keyData.randIndex ? keyData.begIndex + 2
        : keyData.entryCount > 3   ? keyData.begIndex + 3
                                   : keyData.randIndex;

    m = pg_to_move(entries[idx].move, pos);
    if (keyData.entryCount == 3 || !is_draw(pos, m))
        return m;

    return keyData.entryCount > 3 ? pg_to_move(entries[keyData.begIndex + 3].move, pos)
                                  : Move::None;
}

bool PolyBook::can_probe(const Position& pos, Key key) noexcept {

    if (popcount(pieces ^ pos.pieces()) > 6 || popcount(pieces) > pos.count<ALL_PIECE>() + 2
        || key == 0x463B96181691FC9Cull)
        failCount = 0;

    pieces = pos.pieces();
    // Stop probe after 4 times not in the book till position changes
    return failCount <= 4;
}

void PolyBook::find_key(Key key) noexcept {
    keyData = {0, 0, 0, 0, 0, 0};

    std::size_t begIndex = 0;
    std::size_t endIndex = entryCount;

    while (begIndex < endIndex)
    {
        std::size_t midIndex = (begIndex + endIndex) / 2;

        if (entries[midIndex].key < key)
            begIndex = midIndex;
        else if (entries[midIndex].key > key)
            endIndex = midIndex;
        else
        {
            begIndex = std::max<size_t>(midIndex, 0 /*    */ + 4) - 4;
            endIndex = std::min<size_t>(midIndex, entryCount - 4) + 4;
        }

        if (endIndex - begIndex <= 8)
            break;
    }

    while (begIndex < endIndex)
    {
        if (entries[begIndex].key == key)
        {
            while (begIndex > 0 && entries[begIndex - 1].key == key)
                --begIndex;

            get_key_data(begIndex);
            break;
        }
        ++begIndex;
    }
}

void PolyBook::get_key_data(std::size_t begIndex) noexcept {
    static PRNG rng(time(nullptr));

    keyData.entryCount = 1;
    keyData.begIndex   = begIndex;
    keyData.bestIndex  = begIndex;
    keyData.bestWeight = entries[begIndex].weight;
    keyData.sumWeight  = keyData.bestWeight;
    for (std::size_t idx = begIndex + 1; idx < entryCount; ++idx)
    {
        if (entries[idx].key != entries[begIndex].key)
            break;

        ++keyData.entryCount;

        auto weight = entries[idx].weight;
        if (keyData.bestWeight < weight)
        {
            keyData.bestWeight = weight;
            keyData.bestIndex  = idx;
        }
        keyData.sumWeight += weight;
    }

    keyData.randIndex = keyData.bestIndex;

    std::uint16_t randWeight = rng.rand<std::uint16_t>() % keyData.sumWeight;
    std::uint32_t sumWeight  = 0;
    for (std::size_t idx = begIndex; idx < begIndex + keyData.entryCount; ++idx)
    {
        auto weight = entries[idx].weight;
        if (sumWeight <= randWeight && randWeight < sumWeight + weight)
        {
            keyData.randIndex = idx;
            break;
        }
        sumWeight += weight;
    }
}

void PolyBook::show_key_data() const noexcept {

    std::cout << "\nBook entries: " << keyData.entryCount << std::endl;
    for (std::size_t idx = keyData.begIndex; idx < keyData.begIndex + keyData.entryCount; ++idx)
    {
        std::cout << std::setw(2) << std::setfill('0')                  //
                  << idx - keyData.begIndex + 1 << ' ' << entries[idx]  //
                  << " prob: " << std::setw(7) << std::setfill('0') << std::fixed
                  << std::setprecision(4)
                  << (keyData.sumWeight != 0) * 100.0f * entries[idx].weight / keyData.sumWeight
                  << std::endl;
    }
    std::cout << std::endl;
}

}  // namespace DON
